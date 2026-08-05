// Copyright(c) 2011 The LevelDB Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// WriteBatch::rep_ :=
//    sequence: fixed64
//    count: fixed32
//    data: record[count]
// record :=
//    kTypeValue varstring varstring         |
//    kTypeDeletion varstring
// varstring :=
//    len: varint32
//    data: uint8[len]

#include "leveldb/write_batch.h"

#include "db/dbformat.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include <span>
#include <string_view>

#include "leveldb/db.h"

#include "util/coding.h"

namespace leveldb {

// WriteBatch header has an 8-byte sequence number followed by a 4-byte count.
static const size_t kHeader = 12;

template <>
WriteBatch::Range<WriteBatch::UnsafePolicy>::Iterator
WriteBatch::Range<WriteBatch::UnsafePolicy>::begin() const {
  return Iterator(std::string_view(rep_).substr(kHeader));
}

template <>
WriteBatch::Range<WriteBatch::UnsafePolicy>::Iterator
WriteBatch::Range<WriteBatch::UnsafePolicy>::end() const {
  return Iterator(std::string_view(rep_.end(), rep_.end()));
}

template <>
WriteBatch::Range<WriteBatch::SafePolicy>::Iterator
WriteBatch::Range<WriteBatch::SafePolicy>::begin() const {
  return Iterator(std::string_view(rep_).substr(kHeader), state_);
}

template <>
WriteBatch::Range<WriteBatch::SafePolicy>::Iterator
WriteBatch::Range<WriteBatch::SafePolicy>::end() const {
  return Iterator(std::string_view(rep_.end(), rep_.end()), state_);
}

WriteBatch::WriteBatch() { Clear(); }

WriteBatch::~WriteBatch() = default;

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(kHeader);
}

size_t WriteBatch::ApproximateSize() const { return rep_.size(); }

int WriteBatchInternal::Count(const WriteBatch* b) {
  return DecodeFixed<uint32_t>(std::string_view(b->rep_).substr(8));
}

void WriteBatchInternal::SetCount(WriteBatch* b, int n) {
  EncodeFixed<uint32_t>(std::span(&b->rep_[8], 4), n);
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* b) {
  return SequenceNumber(DecodeFixed<uint64_t>(b->rep_));
}

void WriteBatchInternal::SetSequence(WriteBatch* b, SequenceNumber seq) {
  EncodeFixed<uint64_t>(std::span(&b->rep_[0], 8), seq);
}

void WriteBatch::Put(const std::string_view key, const std::string_view value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedBlob<uint32_t>(rep_, key);
  PutLengthPrefixedBlob<uint32_t>(rep_, value);
}

void WriteBatch::Delete(const std::string_view key) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeDeletion));
  PutLengthPrefixedBlob<uint32_t>(rep_, key);
}

void WriteBatch::Append(const WriteBatch& source) {
  WriteBatchInternal::Append(this, &source);
}

std::expected<void, Error> WriteBatchInternal::InsertInto(const WriteBatch* b,
                                                          MemTable* memtable) {
  auto seq = WriteBatchInternal::Sequence(b);
  std::expected<void, Error> status = {};
  for (auto entry : WriteBatch::Range(*b, status)) {
    std::visit(overloaded{[memtable, &seq](WriteBatch::PutEntry& e) {
                            memtable->Add(seq++, kTypeValue, e.key, e.value);
                          },
                          [memtable, &seq](WriteBatch::DeleteEntry& e) {
                            memtable->Add(seq++, kTypeDeletion, e.key,
                                          std::string_view());
                          }},
               entry);
  }
  return status;
}

void WriteBatchInternal::SetContents(WriteBatch* b,
                                     const std::string_view& contents) {
  assert(contents.size() >= kHeader);
  b->rep_.assign(contents.data(), contents.size());
}

void WriteBatchInternal::Append(WriteBatch* dst, const WriteBatch* src) {
  SetCount(dst, Count(dst) + Count(src));
  assert(src->rep_.size() >= kHeader);
  dst->rep_.append(src->rep_.data() + kHeader, src->rep_.size() - kHeader);
}

template <>
void WriteBatch::Range<WriteBatch::SafePolicy>::Iterator::ParseEntry() {
  // Check if current has been parsed
  if (next.data() > current.data()) {
    return;
  }
  if (!next.empty()) {
    char tag = next[0];
    next.remove_prefix(1);
    switch (tag) {
      case kTypeValue: {
        auto key = GetLengthPrefixedBlob<uint32_t>(next);
        if (!key) {
          state_.status_ptr = std::unexpected(
              Error(Error::Code::Corruption, "bad WriteBatch Put"));
          current = next = "";
          return;
        }

        next = key->remaining_input;

        auto value = GetLengthPrefixedBlob<uint64_t>(next);
        if (!value) {
          state_.status_ptr = std::unexpected(
              Error(Error::Code::Corruption, "bad WriteBatch Put"));
          current = next = "";
          return;
        }
        next = value->remaining_input;

        entry = PutEntry{key->value, value->value};
      } break;
      case kTypeDeletion: {
        auto key = GetLengthPrefixedBlob<uint32_t>(next);
        if (!key) {
          state_.status_ptr = std::unexpected(
              Error(Error::Code::Corruption, "bad WriteBatch Put"));
          current = next = "";
          return;
        }

        next = key->remaining_input;
        entry = DeleteEntry{key->value};
      } break;
      default:
        state_.status_ptr = std::unexpected(
            Error(Error::Code::Corruption, "bad WriteBatch Put"));
        current = next = "";
        return;
    }
  }
}

template <>
void WriteBatch::Range<WriteBatch::UnsafePolicy>::Iterator::ParseEntry() {
  // Check if current has been parsed
  if (next.data() > current.data()) {
    return;
  }
  if (!next.empty()) {
    char tag = next[0];
    next.remove_prefix(1);
    switch (tag) {
      case kTypeValue: {
        auto key = GetLengthPrefixedBlob<uint32_t>(next);
        assert(key);
        next = key->remaining_input;

        auto value = GetLengthPrefixedBlob<uint64_t>(next);
        assert(value);
        next = value->remaining_input;

        entry = PutEntry{key->value, value->value};
      } break;
      case kTypeDeletion: {
        auto key = GetLengthPrefixedBlob<uint32_t>(next);
        assert(key);

        next = key->remaining_input;
        entry = DeleteEntry{key->value};
      } break;
      default:
        assert(false);
    }
  }
}

}  // namespace leveldb
