// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
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

WriteBatch::WriteBatch() { Clear(); }

WriteBatch::~WriteBatch() = default;

WriteBatch::Handler::~Handler() = default;

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(kHeader);
}

size_t WriteBatch::ApproximateSize() const { return rep_.size(); }
WriteBatch::Iterator WriteBatch::begin() {
  return WriteBatch::Iterator(std::string_view(rep_).substr(kHeader));
};
WriteBatch::Iterator WriteBatch::end() {
  return WriteBatch::Iterator(std::string_view(rep_.end(), rep_.end()));
};

Status WriteBatch::Iterate(Handler* handler) const {
  std::string_view input(rep_);
  if (input.size() < kHeader) {
    return Status::Corruption("malformed WriteBatch (too small)");
  }

  input.remove_prefix(kHeader);
  int found = 0;
  while (!input.empty()) {
    found++;
    char tag = input[0];
    input.remove_prefix(1);
    switch (tag) {
      case kTypeValue: {
        auto key = GetLengthPrefixedBlob<uint32_t>(input);
        if (!key) {
          return Status::Corruption("bad WriteBatch Put");
        }
        input = key->remaining_input;
        auto value = GetLengthPrefixedBlob<uint64_t>(input);
        if (!value) {
          return Status::Corruption("bad WriteBatch Put");
        }
        input = value->remaining_input;
        handler->Put(key->value, value->value);
      } break;
      case kTypeDeletion: {
        auto key = GetLengthPrefixedBlob<uint32_t>(input);
        if (!key) {
          return Status::Corruption("bad WriteBatch Delete");
        }
        input = key->remaining_input;
        handler->Delete(key->value);
      } break;
      default:
        return Status::Corruption("unknown WriteBatch tag");
    }
  }
  if (found != WriteBatchInternal::Count(this)) {
    return Status::Corruption("WriteBatch has wrong count");
  } else {
    return Status::OK();
  }
}

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

namespace {
class MemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  MemTable* mem_;

  void Put(const std::string_view key, const std::string_view value) override {
    mem_->Add(sequence_, kTypeValue, key, value);
    sequence_++;
  }
  void Delete(const std::string_view key) override {
    mem_->Add(sequence_, kTypeDeletion, key, std::string_view());
    sequence_++;
  }
};
}  // namespace

Status WriteBatchInternal::InsertInto(const WriteBatch* b, MemTable* memtable) {
  MemTableInserter inserter;
  inserter.sequence_ = WriteBatchInternal::Sequence(b);
  inserter.mem_ = memtable;
  return b->Iterate(&inserter);
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

WriteBatch::Iterator& WriteBatch::Iterator::operator++() {
  current = next;
  ParseEntry();
  return *this;
}

WriteBatch::Iterator WriteBatch::Iterator::operator++(int) {
  auto tmp = *this;
  current = next;
  ParseEntry();
  return tmp;
}

void WriteBatch::Iterator::ParseEntry() {
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
