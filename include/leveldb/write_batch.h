// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// WriteBatch holds a collection of updates to apply atomically to a DB.
//
// The updates are applied in the order in which they are added
// to the WriteBatch.  For example, the value of "key" will be "v3"
// after the following batch is written:
//
//    batch.Put("key", "v1");
//    batch.Delete("key");
//    batch.Put("key", "v2");
//    batch.Put("key", "v3");
//
// Multiple threads can invoke const methods on a WriteBatch without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same WriteBatch must use
// external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_WRITE_BATCH_H_
#define STORAGE_LEVELDB_INCLUDE_WRITE_BATCH_H_

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

#include "leveldb/export.h"
#include "leveldb/status.h"

namespace leveldb {

class LEVELDB_EXPORT WriteBatch {
 public:
  class LEVELDB_EXPORT Handler {
   public:
    virtual ~Handler();
    virtual void Put(const std::string_view key,
                     const std::string_view value) = 0;
    virtual void Delete(const std::string_view key) = 0;
  };

  WriteBatch();

  // Intentionally copyable.
  WriteBatch(const WriteBatch&) = default;
  WriteBatch& operator=(const WriteBatch&) = default;

  ~WriteBatch();

  // Store the mapping "key->value" in the database.
  void Put(const std::string_view key, const std::string_view value);

  // If the database contains a mapping for "key", erase it.  Else do nothing.
  void Delete(const std::string_view key);

  // Clear all updates buffered in this batch.
  void Clear();

  // The size of the database changes caused by this batch.
  //
  // This number is tied to implementation details, and may change across
  // releases. It is intended for LevelDB usage metrics.
  size_t ApproximateSize() const;

  // Copies the operations in "source" to this batch.
  //
  // This runs in O(source size) time. However, the constant factor is better
  // than calling Iterate() over the source batch with a Handler that replicates
  // the operations into this batch.
  void Append(const WriteBatch& source);

  // Support for iterating over the contents of a batch.
  Status Iterate(Handler* handler) const;
  struct DeleteEntry {
    std::string_view key;
  };
  struct PutEntry {
    std::string_view key;
    std::string_view value;
  };
  typedef std::variant<DeleteEntry, PutEntry> Entry;
  class Iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Entry;
    using pointer = Entry*;
    using referance = Entry&;

    Iterator() = default;
    Iterator(std::string_view view) : current(view), next(view) {
      ParseEntry();
    };

    referance operator*() const { return entry; };
    pointer operator->() const { return &entry; };

    Iterator& operator++();    // prefix ++
    Iterator operator++(int);  // postfix ++

    friend bool operator==(const Iterator& a, const Iterator& b) {
      return a.current == b.current;
    }
    friend bool operator!=(const Iterator& a, const Iterator& b) {
      return a.current != b.current;
    }

   private:
    void ParseEntry();
    mutable std::string_view current;
    mutable std::string_view next;
    mutable value_type entry;
  };

  Iterator begin();
  Iterator end();

 private:
  friend class WriteBatchInternal;

  std::string rep_;  // See comment in write_batch.cc for the format of rep_
};
static_assert(std::ranges::range<WriteBatch>,
              "WriteBatch does not fulfill the requirerments for a std::ranges::range");
static_assert(std::input_iterator<WriteBatch::Iterator>,
              "WriteBatch::Iterator does not fulfill the requirements for a "
              "std::input_iterator");
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_WRITE_BATCH_H_
