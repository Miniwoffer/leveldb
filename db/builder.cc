// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/builder.h"

#include "db/dbformat.h"
#include "db/filename.h"
#include "db/table_cache.h"
#include "db/version_edit.h"

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"

namespace leveldb {

std::expected<void, Error> BuildTable(const std::string& dbname, Env* env,
                                      const Options& options,
                                      TableCache* table_cache, Iterator* iter,
                                      FileMetaData* meta) {
  std::expected<void, Error> result;
  meta->file_size = 0;
  iter->SeekToFirst();

  std::string fname = TableFileName(dbname, meta->number);
  if (iter->Valid()) {
    WritableFile* file;
    if (auto ret = env->NewWritableFile(fname)) {
      file = ret.value();
    } else {
      return std::unexpected(ret.error());
    }

    TableBuilder* builder = new TableBuilder(options, file);
    meta->smallest.DecodeFrom(iter->key());
    std::string_view key;
    for (; iter->Valid(); iter->Next()) {
      key = iter->key();
      builder->Add(key, iter->value());
    }
    if (!key.empty()) {
      meta->largest.DecodeFrom(key);
    }

    // Finish and check for builder errors
    result = builder->Finish();
    if (result) {
      meta->file_size = builder->FileSize();
      assert(meta->file_size > 0);
    }
    delete builder;

    // Finish and check for file errors
    result = result.and_then([file]() {
      return file->Sync().and_then([file]() { return file->Close(); });
    });
    delete file;
    file = nullptr;

    if (result) {
      // Verify that the table is usable
      Iterator* it = table_cache->NewIterator(ReadOptions(), meta->number,
                                              meta->file_size);
      result = it->error();
      delete it;
    }
  }

  // Check for input iterator errors
  if (!iter->error()) {
    result = iter->error();
  }

  if (result && meta->file_size > 0) {
    // Keep it
  } else {
    env->RemoveFile(fname);
  }

  return result;
}

}  // namespace leveldb
