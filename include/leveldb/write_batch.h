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

#include <cmath>
#include <cstddef>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

#include "leveldb/error.h"
#include "leveldb/export.h"

namespace leveldb {
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
class LEVELDB_EXPORT WriteBatch {
 public:
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

  struct DeleteEntry {
    std::string_view key;
  };
  struct PutEntry {
    std::string_view key;
    std::string_view value;
  };
  typedef std::variant<DeleteEntry, PutEntry> Entry;
  struct UnsafePolicy {
    static constexpr bool is_safe = false;
    struct State {};
  };

  struct SafePolicy {
    static constexpr bool is_safe = true;
    struct State {
      std::expected<void, Error>& status_ptr;
      State(std::expected<void, Error>& status_ptr) : status_ptr(status_ptr) {};
    };
  };

  template <typename Policy>
  class Range {
    std::string_view rep_;
    [[no_unique_address]] typename Policy::State state_;

   public:
    class Iterator {
      [[no_unique_address]] typename Policy::State state_;

     public:
      using iterator_category = std::input_iterator_tag;
      using difference_type = std::ptrdiff_t;
      using value_type = Entry;
      using pointer = Entry*;
      using reference = Entry&;

      Iterator() = default;
      Iterator(std::string_view view)
        requires(!Policy::is_safe)
          : current(view), next(view) {
        ParseEntry();
      };
      Iterator(std::string_view view, Policy::State state)
        requires(Policy::is_safe)
          : current(view), next(view), state_(state) {
        ParseEntry();
      };

      reference operator*() const { return entry; };
      pointer operator->() const { return &entry; };

      Iterator operator++(int) {
        auto tmp = *this;
        current = next;
        ParseEntry();
        return tmp;
      }
      // prefix ++
      Iterator& operator++() {
        current = next;
        ParseEntry();
        return *this;
      };  // postfix ++

      bool operator==(const Iterator& b) const { return current == b.current; }
      bool operator!=(const Iterator& b) const { return current != b.current; }

     private:
      void ParseEntry();
      mutable std::string_view current;
      mutable std::string_view next;
      mutable value_type entry;
    };

    Range(const std::string_view rep_, std::expected<void, Error>& status)
      requires(Policy::is_safe)
        : rep_(rep_), state_(status) {
      state_.status_ptr = status;
    };
    Range(const WriteBatch& wb, std::expected<void, Error>& status)
      requires(Policy::is_safe)
        : rep_(wb.rep_), state_(status) {};

    Range(const std::string_view rep_)
      requires(!Policy::is_safe)
        : rep_(rep_) {};
    Range(const WriteBatch& wb)
      requires(!Policy::is_safe)
        : rep_(wb.rep_) {};

    Iterator begin() const;
    Iterator end() const;
  };
  Range(const WriteBatch) -> Range<UnsafePolicy>;
  Range(const WriteBatch, std::expected<void, Error>&) -> Range<SafePolicy>;

 private:
  friend class WriteBatchInternal;

  std::string rep_;  // See comment in write_batch.cc for the format of rep_
};
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_WRITE_BATCH_H_
