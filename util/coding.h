// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Endian-neutral encoding:
// * Fixed-length numbers are encoded with least-significant byte first
// * In addition we support variable length "varint" encoding
// * Strings are encoded prefixed by their length in varint format

#ifndef STORAGE_LEVELDB_UTIL_CODING_H_
#define STORAGE_LEVELDB_UTIL_CODING_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <regex.h>
#include <span>
#include <string>
#include <string_view>

#include "port/port.h"
#include "util/coding_v2.h"

namespace leveldb {

// Standard Get... routines parse a value from the beginning of a
// std::string_view and advance the slice past the parsed value.
bool GetVarint32(std::string_view* input, uint32_t* value);
bool GetVarint64(std::string_view* input, uint64_t* value);
std::optional<std::string_view> GetLengthPrefixedView(std::string_view& input);

// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// nullptr on error.  These routines only look at bytes in the range
// [p..limit-1]
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* v);
const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* v);

// Returns the length of the varint32 or varint64 encoding of "v"
int VarintLength(uint64_t v);

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint32_t DecodeFixed32(const char* ptr) {
  return DecodeFixed<uint32_t>(std::string_view(ptr, 4));
}

inline uint64_t DecodeFixed64(const char* ptr) {
  return DecodeFixed<uint64_t>(std::string_view(ptr, 8));
}

inline const char* GetVarint32Ptr(const char* p, const char* limit,
                                  uint32_t* value) {
  std::string_view sv(p, limit - p);
  auto result = GetVarint<uint32_t>(sv);
  if (result) {
    *value = result->value;
    return result->remaining_input.data();
  }
  return nullptr;
}

// Internal routine for use by fallback path of GetVarint32Ptr
std::optional<uint32_t> GetVarint32PtrFallback(std::string_view& data);
inline std::optional<uint32_t> GetVarint32Ptr(std::string_view& data) {
  auto result = GetVarint<uint32_t>(data);
  if (result) {
    data = result->remaining_input;
    return result->value;
  }
  return {};
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_CODING_H_
