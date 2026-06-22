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
// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// nullptr on error.  These routines only look at bytes in the range
// [p..limit-1]

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

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_CODING_H_
