// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Slice is a simple structure containing a pointer into some external
// storage and a size.  The user of a Slice must ensure that the slice
// is not used after the corresponding external storage has been
// deallocated.
//
// Multiple threads can invoke const methods on a Slice without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Slice must use
// external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_SLICE_H_
#define STORAGE_LEVELDB_INCLUDE_SLICE_H_

#include <cassert>
#include <string>
#include <string_view>

#include "leveldb/export.h"

namespace leveldb {

class LEVELDB_EXPORT Slice : public std::string_view {
 public:
  using std::string_view::string_view;

  // constexpr Slice(std::string_view sv) noexcept : std::string_view(sv) {}
  constexpr Slice(const std::string& s) noexcept : std::string_view(s) {}

  // Change this slice to refer to an empty array
  void Clear() { *this = {}; }

  // Return a string that contains the copy of the referenced data.
  std::string ToString() const { return std::string{data(), size()}; }
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_SLICE_H_
