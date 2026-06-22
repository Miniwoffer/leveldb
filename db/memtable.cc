// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/memtable.h"

#include "db/dbformat.h"
#include <cstdint>
#include <optional>

#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"

#include "util/coding.h"

namespace leveldb {

static std::string_view GetLengthPrefixedView(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
  return std::string_view(p, len);
}

MemTable::MemTable(const InternalKeyComparator& comparator)
    : comparator_(comparator), refs_(0), table_(comparator_, &arena_) {}

MemTable::~MemTable() { assert(refs_ == 0); }

size_t MemTable::ApproximateMemoryUsage() { return arena_.MemoryUsage(); }

int MemTable::KeyComparator::operator()(const char* aptr,
                                        const char* bptr) const {
  // Internal keys are encoded as length-prefixed strings.
  std::string_view a = GetLengthPrefixedView(aptr);
  std::string_view b = GetLengthPrefixedView(bptr);
  return comparator.Compare(a, b);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
static const char* EncodeKey(std::string& scratch,
                             const std::string_view& target) {
  scratch.clear();
  PutLengthPrefixedBlob<uint32_t>(scratch, target);
  return scratch.data();
}
static const char* EncodeKey(std::string* scratch,
                             const std::string_view& target) {
  return EncodeKey(*scratch, target);
}

class MemTableIterator : public Iterator {
 public:
  explicit MemTableIterator(MemTable::Table* table) : iter_(table) {}

  MemTableIterator(const MemTableIterator&) = delete;
  MemTableIterator& operator=(const MemTableIterator&) = delete;

  ~MemTableIterator() override = default;

  bool Valid() const override { return iter_.Valid(); }
  void Seek(const std::string_view& k) override {
    iter_.Seek(EncodeKey(&tmp_, k));
  }
  void SeekToFirst() override { iter_.SeekToFirst(); }
  void SeekToLast() override { iter_.SeekToLast(); }
  void Next() override { iter_.Next(); }
  void Prev() override { iter_.Prev(); }
  std::string_view key() const override {
    return GetLengthPrefixedView(iter_.key());
  }
  std::string_view value() const override {
    std::string_view key_slice = GetLengthPrefixedView(iter_.key());
    return GetLengthPrefixedView(key_slice.data() + key_slice.size());
  }

  Status status() const override { return Status::OK(); }

 private:
  MemTable::Table::Iterator iter_;
  std::string tmp_;  // For passing to EncodeKey
};

Iterator* MemTable::NewIterator() { return new MemTableIterator(&table_); }

void MemTable::Add(SequenceNumber s, ValueType type,
                   const std::string_view& key, const std::string_view& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  tag          : uint64((sequence << 8) | type)
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 8;
  const size_t encoded_len = VarintLength(internal_key_size) +
                             internal_key_size + VarintLength(val_size) +
                             val_size;

  std::span mem(arena_.Allocate(encoded_len), encoded_len);
  auto buff = mem;

  buff = EncodeVarint<uint32_t>(buff, internal_key_size);

  std::memcpy(buff.data(), key.data(), key_size);
  buff = buff.subspan(key_size);

  buff = EncodeFixed<uint64_t>(buff, (s << 8) | type);

  buff = EncodeLengthPrefixedString<uint32_t>(buff, value);
  assert(buff.empty());
  table_.Insert(reinterpret_cast<const char*>(mem.data()));
}

std::optional<std::expected<std::string, Status>> MemTable::Get(
    const LookupKey& key) {
  std::string_view memkey = key.memtable_key();
  Table::Iterator iter(&table_);
  iter.Seek(memkey.data());
  if (iter.Valid()) {
    // entry format is:
    //    klength  varint32
    //    userkey  char[klength]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    const char* entry = iter.key();
    uint32_t key_length;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (comparator_.comparator.user_comparator()->Compare(
            std::string_view(key_ptr, key_length - 8), key.user_key()) == 0) {
      // Correct user key
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
      switch (static_cast<ValueType>(tag & 0xff)) {
        case kTypeValue: {
          return std::string(GetLengthPrefixedView(key_ptr + key_length));
        }
        case kTypeDeletion:
          return std::unexpected(Status::NotFound(std::string_view()));
      }
    }
  }
  return std::nullopt;
}

}  // namespace leveldb
