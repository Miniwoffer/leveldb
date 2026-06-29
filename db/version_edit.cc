// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_edit.h"

#include "db/version_set.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "util/coding.h"

namespace leveldb {

// Tag numbers for serialized VersionEdit.  These numbers are written to
// disk and should not be changed.
enum Tag {
  kComparator = 1,
  kLogNumber = 2,
  kNextFileNumber = 3,
  kLastSequence = 4,
  kCompactPointer = 5,
  kDeletedFile = 6,
  kNewFile = 7,
  // 8 was used for large value refs
  kPrevLogNumber = 9
};

void VersionEdit::Clear() {
  comparator_.clear();
  log_number_ = 0;
  prev_log_number_ = 0;
  last_sequence_ = 0;
  next_file_number_ = 0;
  has_comparator_ = false;
  has_log_number_ = false;
  has_prev_log_number_ = false;
  has_next_file_number_ = false;
  has_last_sequence_ = false;
  compact_pointers_.clear();
  deleted_files_.clear();
  new_files_.clear();
}

void VersionEdit::EncodeTo(std::string* dst) const { EncodeTo(*dst); }
void VersionEdit::EncodeTo(std::string& dst) const {
  if (has_comparator_) {
    PutVarint<uint32_t>(dst, kComparator);
    PutLengthPrefixedBlob<uint32_t>(dst, comparator_);
  }
  if (has_log_number_) {
    PutVarint<uint32_t>(dst, kLogNumber);
    PutVarint<uint64_t>(dst, log_number_);
  }
  if (has_prev_log_number_) {
    PutVarint<uint32_t>(dst, kPrevLogNumber);
    PutVarint<uint64_t>(dst, prev_log_number_);
  }
  if (has_next_file_number_) {
    PutVarint<uint32_t>(dst, kNextFileNumber);
    PutVarint<uint64_t>(dst, next_file_number_);
  }
  if (has_last_sequence_) {
    PutVarint<uint32_t>(dst, kLastSequence);
    PutVarint<uint64_t>(dst, last_sequence_);
  }

  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    PutVarint<uint32_t>(dst, kCompactPointer);
    PutVarint<uint32_t>(dst, compact_pointers_[i].first);  // level
    PutLengthPrefixedBlob<uint32_t>(dst, compact_pointers_[i].second.Encode());
  }

  for (const auto& deleted_file_kvp : deleted_files_) {
    PutVarint<uint32_t>(dst, kDeletedFile);
    PutVarint<uint32_t>(dst, deleted_file_kvp.first);   // level
    PutVarint<uint64_t>(dst, deleted_file_kvp.second);  // file number
  }

  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    PutVarint<uint32_t>(dst, kNewFile);
    PutVarint<uint32_t>(dst, new_files_[i].first);  // level
    PutVarint<uint64_t>(dst, f.number);
    PutVarint<uint64_t>(dst, f.file_size);
    PutLengthPrefixedBlob<uint32_t>(dst, f.smallest.Encode());
    PutLengthPrefixedBlob<uint32_t>(dst, f.largest.Encode());
  }
}

static std::optional<InternalKey> GetInternalKey(std::string_view& input) {
  auto str = GetLengthPrefixedBlob<uint32_t>(input);
  if (str) {
    input = str->remaining_input;
    // TODO: remove convertion hack
    InternalKey key;
    if (key.DecodeFrom(str->value)) {
      return key;
    }
  }
  return {};
}

static std::optional<uint32_t> GetLevel(std::string_view& input) {
  if (auto v = GetVarint<uint32_t>(input)) {
    input = v->remaining_input;
    if (v->value < config::kNumLevels) {
      return v->value;
    }
  }
  return {};
}

Error VersionEdit::DecodeFrom(const std::string_view& src) {
  Clear();
  std::string_view input = src;
  const char* msg = nullptr;
  std::optional<DecodingResult<uint32_t>> tag;

  // Temporary storage for parsing
  FileMetaData f;

  while (msg == nullptr && (tag = GetVarint<uint32_t>(input))) {
    input = tag->remaining_input;
    switch (tag->value) {
      case kComparator: {
        std::string_view input_ = input;
        auto name = GetLengthPrefixedBlob<uint32_t>(input);
        if (name) {
          comparator_ = name->value;
          has_comparator_ = true;
          input = name->remaining_input;
        } else {
          msg = "comparator name";
        }
      } break;

      case kLogNumber:
        if (auto log_number = GetVarint<uint64_t>(input)) {
          log_number_ = log_number->value;
          input = log_number->remaining_input;
          has_log_number_ = true;
        } else {
          msg = "log number";
        }
        break;

      case kPrevLogNumber:
        if (auto prev_log_number = GetVarint<uint64_t>(input)) {
          prev_log_number_ = prev_log_number->value;
          input = prev_log_number->remaining_input;
          has_prev_log_number_ = true;
        } else {
          msg = "previous log number";
        }
        break;

      case kNextFileNumber:
        if (auto next_file_number = GetVarint<uint64_t>(input)) {
          next_file_number_ = next_file_number->value;
          input = next_file_number->remaining_input;
          has_next_file_number_ = true;
        } else {
          msg = "next file number";
        }
        break;

      case kLastSequence:
        if (auto last_sequence = GetVarint<uint64_t>(input)) {
          last_sequence_ = last_sequence->value;
          input = last_sequence->remaining_input;
          has_last_sequence_ = true;
        } else {
          msg = "last sequence number";
        }
        break;

      case kCompactPointer:
        if (auto level = GetLevel(input)) {
          if (auto key = GetInternalKey(input)) {
            compact_pointers_.push_back(std::make_pair(*level, *key));
            continue;
          }
        }
        msg = "compaction pointer";
        break;

      case kDeletedFile:
        if (auto level = GetLevel(input)) {
          auto number = GetVarint<uint64_t>(input);
          if (number) {
            input = number->remaining_input;
            deleted_files_.insert(std::make_pair(*level, number->value));
            continue;
          }
        }
        msg = "deleted file";
        break;

      case kNewFile:

        if (auto level = GetLevel(input)) {
          auto number = GetVarint<uint64_t>(input);
          if (number) {
            f.number = number->value;
            input = number->remaining_input;
            auto file_size = GetVarint<uint64_t>(input);
            if (file_size) {
              f.file_size = file_size->value;
              input = file_size->remaining_input;
              if (auto smallest = GetInternalKey(input)) {
                f.smallest = *smallest;
                if (auto largest = GetInternalKey(input)) {
                  f.largest = *largest;
                  new_files_.push_back(std::make_pair(*level, f));
                  continue;
                }
              }
            }
          }
        }
        msg = "new-file entry";
        break;

      default:
        msg = "unknown tag";
        break;
    }
  }

  if (msg == nullptr && !input.empty()) {
    msg = "invalid tag";
  }

  Error result;
  if (msg != nullptr) {
    result = Error::Corruption("VersionEdit", msg);
  }
  return result;
}

std::string VersionEdit::DebugString() const {
  std::string r;
  r.append("VersionEdit {");
  if (has_comparator_) {
    r.append("\n  Comparator: ");
    r.append(comparator_);
  }
  if (has_log_number_) {
    r.append("\n  LogNumber: ");
    AppendNumberTo(&r, log_number_);
  }
  if (has_prev_log_number_) {
    r.append("\n  PrevLogNumber: ");
    AppendNumberTo(&r, prev_log_number_);
  }
  if (has_next_file_number_) {
    r.append("\n  NextFile: ");
    AppendNumberTo(&r, next_file_number_);
  }
  if (has_last_sequence_) {
    r.append("\n  LastSeq: ");
    AppendNumberTo(&r, last_sequence_);
  }
  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    r.append("\n  CompactPointer: ");
    AppendNumberTo(&r, compact_pointers_[i].first);
    r.append(" ");
    r.append(compact_pointers_[i].second.DebugString());
  }
  for (const auto& deleted_files_kvp : deleted_files_) {
    r.append("\n  RemoveFile: ");
    AppendNumberTo(&r, deleted_files_kvp.first);
    r.append(" ");
    AppendNumberTo(&r, deleted_files_kvp.second);
  }
  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    r.append("\n  AddFile: ");
    AppendNumberTo(&r, new_files_[i].first);
    r.append(" ");
    AppendNumberTo(&r, f.number);
    r.append(" ");
    AppendNumberTo(&r, f.file_size);
    r.append(" ");
    r.append(f.smallest.DebugString());
    r.append(" .. ");
    r.append(f.largest.DebugString());
  }
  r.append("\n}\n");
  return r;
}

}  // namespace leveldb
