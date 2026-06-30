// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_INCLUDE_DB_H_
#define STORAGE_LEVELDB_INCLUDE_DB_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "leveldb/error.h"
#include "leveldb/export.h"
#include "leveldb/iterator.h"
#include "leveldb/options.h"

namespace leveldb {

// Update CMakeLists.txt if you change these
static const int kMajorVersion = 1;
static const int kMinorVersion = 23;

struct Options;
struct ReadOptions;
struct WriteOptions;
class WriteBatch;

// Abstract handle to particular state of a DB.
// A Snapshot is an immutable object and can therefore be safely
// accessed from multiple threads without any external synchronization.
class LEVELDB_EXPORT Snapshot {
 protected:
  virtual ~Snapshot();
};

// A range of keys
struct LEVELDB_EXPORT Range {
  Range() = default;
  Range(const std::string_view s, const std::string_view l)
      : start(s), limit(l) {}

  std::string_view start;  // Included in the range
  std::string_view limit;  // Not included in the range
};

// A DB is a persistent ordered map from keys to values.
// A DB is safe for concurrent access from multiple threads without
// any external synchronization.
class LEVELDB_EXPORT DB {
 public:
  // Open the database with the specified "name".
  // Stores a pointer to a heap-allocated database in *dbptr and returns
  // OK on success.
  // Stores nullptr in *dbptr and returns a non-OK status on error.
  // Caller should delete *dbptr when it is no longer needed.
  static std::expected<std::shared_ptr<DB>, Error> Open(
      const Options& options, const std::string_view name);

  DB() = default;

  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  virtual ~DB();

  // Set the database entry for "key" to "value".  Returns OK on success,
  // and a non-OK status on error.
  // Note: consider setting options.sync = true.
  virtual std::expected<void, Error> Put(const WriteOptions& options,
                                         const std::string_view key,
                                         const std::string_view value) = 0;

  // Remove the database entry (if any) for "key".  Returns OK on
  // success, and a non-OK status on error.  It is not an error if "key"
  // did not exist in the database.
  // Note: consider setting options.sync = true.
  virtual std::expected<void, Error> Delete(const WriteOptions& options,
                                            const std::string_view key) = 0;

  // Apply the specified updates to the database.
  // Returns OK on success, non-OK on failure.
  // Note: consider setting options.sync = true.
  virtual std::expected<void, Error> Write(const WriteOptions& options,
                                           WriteBatch* updates) = 0;

  // If the database contains an entry for "key" returns string.
  //
  // If there is no entry for "key" returns
  // a status for which Error::IsNotFound() returns true.
  //
  // May return some other Error on an error.
  virtual std::expected<std::string, Error> Get(const ReadOptions& options,
                                                const std::string_view key) = 0;

  // Return a heap-allocated iterator over the contents of the database.
  // The result of NewIterator() is initially invalid (caller must
  // call one of the Seek methods on the iterator before using it).
  //
  // Caller should delete the iterator when it is no longer needed.
  // The returned iterator should be deleted before this db is deleted.
  virtual std::unique_ptr<Iterator> NewIterator(const ReadOptions& options) = 0;

  // Return a handle to the current DB state.  Iterators created with
  // this handle will all observe a stable snapshot of the current DB
  // state.  The caller must either delete or clear(ptr = {}) the shared_ptr
  // once done
  virtual std::shared_ptr<const Snapshot> GetSnapshot() = 0;

  // DB implementations can export properties about their state
  // via this method.  If "property" is a valid property understood by this
  // DB implementation, fills "*value" with its current value and returns
  // true.  Otherwise returns false.
  //
  //
  // Valid property names include:
  //
  //  "leveldb.num-files-at-level<N>" - return the number of files at level <N>,
  //     where <N> is an ASCII representation of a level number (e.g. "0").
  //  "leveldb.stats" - returns a multi-line string that describes statistics
  //     about the internal operation of the DB.
  //  "leveldb.sstables" - returns a multi-line string that describes all
  //     of the sstables that make up the db contents.
  //  "leveldb.approximate-memory-usage" - returns the approximate number of
  //     bytes of memory in use by the DB.
  virtual std::optional<std::string> GetProperty(
      const std::string_view property) = 0;

  // For each range given in ranges calculates the approximate file system
  // space used that range
  //
  // Note that the returned sizes measure file system space usage, so
  // if the user data compresses by a factor of ten, the returned
  // sizes will be one-tenth the size of the corresponding user data size.
  //
  // The results may not include the sizes of recently written data.

  virtual std::vector<uint64_t> GetApproximateSizes(
      std::span<const Range> ranges) = 0;

  // Compact the underlying storage for the key range [start, end].
  // In particular, deleted and overwritten versions are discarded,
  // and the data is rearranged to reduce the cost of operations
  // needed to access the data.  This operation should typically only
  // be invoked by users who understand the underlying implementation.
  //
  // start.empty() is treated as a key before all keys in the database.
  // limit.empty() is treated as a key after all keys in the database.
  virtual void Compact(const Range range = {}) = 0;
};

// Destroy the contents of the specified database.
// Be very careful using this method.
//
// Note: For backwards compatibility, if DestroyDB is unable to list the
// database files, Error(Error::Code::Ok) will still be returned masking this
// failure.
LEVELDB_EXPORT Error DestroyDB(const std::string& name, const Options& options);

// If a DB cannot be opened, you may attempt to call this method to
// resurrect as much of the contents of the database as possible.
// Some data may be lost, so be careful when calling this function
// on a database that contains important information.
LEVELDB_EXPORT Error RepairDB(const std::string& dbname,
                              const Options& options);

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_DB_H_
