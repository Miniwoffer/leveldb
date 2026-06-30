// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/table_cache.h"

#include "db/filename.h"
#include <array>

#include "leveldb/env.h"
#include "leveldb/table.h"

#include "util/coding.h"

namespace leveldb {

struct TableAndFile {
  RandomAccessFile* file;
  Table* table;
};

static void DeleteEntry(const std::string_view& key, void* value) {
  TableAndFile* tf = reinterpret_cast<TableAndFile*>(value);
  delete tf->table;
  delete tf->file;
  delete tf;
}

static void UnrefEntry(void* arg1, void* arg2) {
  Cache* cache = reinterpret_cast<Cache*>(arg1);
  Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
  cache->Release(h);
}

TableCache::TableCache(const std::string& dbname, const Options& options,
                       int entries)
    : env_(options.env),
      dbname_(dbname),
      options_(options),
      cache_(NewLRUCache(entries)) {}

TableCache::~TableCache() { delete cache_; }

std::expected<Cache::Handle*, Error> TableCache::FindTable(uint64_t file_number,
                                                           uint64_t file_size) {
  std::array<char, sizeof(file_number)> buf;
  EncodeFixed<uint64_t, char>(buf, file_number);
  std::string_view key(buf);
  if (auto lookup_res = cache_->Lookup(key)) {
    return *lookup_res;
  }

  std::string fname = TableFileName(dbname_, file_number);
  RandomAccessFile* file = nullptr;
  Table* table = nullptr;
  Error e = env_->NewRandomAccessFile(fname, &file);
  if (!e.ok()) {
    std::string old_fname = SSTTableFileName(dbname_, file_number);
    if (env_->NewRandomAccessFile(old_fname, &file).ok()) {
      e = Error(Error::Code::Ok);
    }
  }
  if (e.ok()) {
    e = Table::Open(options_, file, file_size, &table);
  }

  if (!e.ok()) {
    assert(table == nullptr);
    delete file;
    return std::unexpected(std::move(e));
    // We do not cache error results so that if the error is transient,
    // or somebody repairs the file, we recover automatically.
  }

  TableAndFile* tf = new TableAndFile;
  tf->file = file;
  tf->table = table;
  if (auto handle = cache_->Insert(key, tf, 1, &DeleteEntry)) {
    return *handle;
  } else {
    return std::unexpected(Error(Error::Code::NotFound));
  }
}

Iterator* TableCache::NewIterator(const ReadOptions& options,
                                  uint64_t file_number, uint64_t file_size,
                                  Table** tableptr) {
  if (tableptr != nullptr) {
    *tableptr = nullptr;
  }

  if (auto handle = FindTable(file_number, file_size)) {
    Table* table =
        reinterpret_cast<TableAndFile*>(cache_->Value(*handle))->table;
    Iterator* result = table->NewIterator(options);
    result->RegisterCleanup(&UnrefEntry, cache_, *handle);
    if (tableptr != nullptr) {
      *tableptr = table;
    }
    return result;
  } else {
    return NewErrorIterator(handle.error());
  }
}

std::expected<std::string, Error> TableCache::Get(
    const ReadOptions& options, uint64_t file_number, uint64_t file_size,
    const std::string_view& k,
    std::function<std::expected<std::string, Error>(const std::string_view&,
                                                    const std::string_view&)>
        handle_result) {
  if (auto handle = FindTable(file_number, file_size)) {
    Table* t = reinterpret_cast<TableAndFile*>(cache_->Value(*handle))->table;
    auto res = t->InternalGet(options, k, handle_result);
    cache_->Release(*handle);
    return res;
  } else {
    return std::unexpected(std::move(handle.error()));
  }
}

void TableCache::Evict(uint64_t file_number) {
  std::array<char, sizeof(file_number)> buf;
  EncodeFixed<uint64_t, char>(buf, file_number);
  cache_->Erase(std::string_view(buf));
}

}  // namespace leveldb
