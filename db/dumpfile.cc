// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/dumpfile.h"

#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/version_edit.h"
#include "db/write_batch_internal.h"
#include <cstdio>

#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "leveldb/options.h"
#include "leveldb/table.h"
#include "leveldb/write_batch.h"

#include "util/logging.h"

namespace leveldb {

namespace {

bool GuessType(const std::string& fname, FileType* type) {
  size_t pos = fname.rfind('/');
  std::string basename;
  if (pos == std::string::npos) {
    basename = fname;
  } else {
    basename = std::string(fname.data() + pos + 1, fname.size() - pos - 1);
  }
  uint64_t ignored;
  return ParseFileName(basename, &ignored, type);
}

// Notified when log reader encounters corruption.
class CorruptionReporter : public log::Reader::Reporter {
 public:
  void Corruption(size_t bytes, const Error& status) override {
    std::string r = "corruption: ";
    AppendNumberTo(&r, bytes);
    r += " bytes; ";
    r += status.ToString();
    r.push_back('\n');
    dst_->Append(r);
  }

  WritableFile* dst_;
};

// Print contents of a log file. (*func)() is called on every record.
Error PrintLogContents(Env* env, const std::string& fname,
                       void (*func)(uint64_t, std::string_view, WritableFile*),
                       WritableFile* dst) {
  SequentialFile* file;
  Error e = env->NewSequentialFile(fname, &file);
  if (!e.ok()) {
    return e;
  }
  CorruptionReporter reporter;
  reporter.dst_ = dst;
  log::Reader reader(file, &reporter, true, 0);
  std::string_view record;
  std::string scratch;
  while (reader.ReadRecord(&record, &scratch)) {
    (*func)(reader.LastRecordOffset(), record, dst);
  }
  delete file;
  return Error(Error::Code::Ok);
}

// Called on every item found in a WriteBatch.
class WriteBatchItemPrinter : public WriteBatch::Handler {
 public:
  void Put(const std::string_view key, const std::string_view value) override {
    std::string r = "  put '";
    AppendEscapedStringTo(&r, key);
    r += "' '";
    AppendEscapedStringTo(&r, value);
    r += "'\n";
    dst_->Append(r);
  }
  void Delete(const std::string_view key) override {
    std::string r = "  del '";
    AppendEscapedStringTo(&r, key);
    r += "'\n";
    dst_->Append(r);
  }

  WritableFile* dst_;
};

// Called on every log record (each one of which is a WriteBatch)
// found in a kLogFile.
static void WriteBatchPrinter(uint64_t pos, std::string_view record,
                              WritableFile* dst) {
  std::string r = "--- offset ";
  AppendNumberTo(&r, pos);
  r += "; ";
  if (record.size() < 12) {
    r += "log record length ";
    AppendNumberTo(&r, record.size());
    r += " is too small\n";
    dst->Append(r);
    return;
  }
  WriteBatch batch;
  WriteBatchInternal::SetContents(&batch, record);
  r += "sequence ";
  AppendNumberTo(&r, WriteBatchInternal::Sequence(&batch));
  r.push_back('\n');
  dst->Append(r);
  WriteBatchItemPrinter batch_item_printer;
  batch_item_printer.dst_ = dst;
  Error e = batch.Iterate(&batch_item_printer);
  if (!e.ok()) {
    dst->Append("  error: " + e.ToString() + "\n");
  }
}

Error DumpLog(Env* env, const std::string& fname, WritableFile* dst) {
  return PrintLogContents(env, fname, WriteBatchPrinter, dst);
}

// Called on every log record (each one of which is a WriteBatch)
// found in a kDescriptorFile.
static void VersionEditPrinter(uint64_t pos, std::string_view record,
                               WritableFile* dst) {
  std::string r = "--- offset ";
  AppendNumberTo(&r, pos);
  r += "; ";
  VersionEdit edit;
  Error e = edit.DecodeFrom(record);
  if (!e.ok()) {
    r += e.ToString();
    r.push_back('\n');
  } else {
    r += edit.DebugString();
  }
  dst->Append(r);
}

Error DumpDescriptor(Env* env, const std::string& fname, WritableFile* dst) {
  return PrintLogContents(env, fname, VersionEditPrinter, dst);
}

Error DumpTable(Env* env, const std::string& fname, WritableFile* dst) {
  uint64_t file_size;
  RandomAccessFile* file = nullptr;
  Table* table = nullptr;
  Error e = env->GetFileSize(fname, &file_size);
  if (e.ok()) {
    e = env->NewRandomAccessFile(fname, &file);
  }
  if (e.ok()) {
    // We use the default comparator, which may or may not match the
    // comparator used in this database. However this should not cause
    // problems since we only use Table operations that do not require
    // any comparisons.  In particular, we do not call Seek or Prev.
    e = Table::Open(Options(), file, file_size, &table);
  }
  if (!e.ok()) {
    delete table;
    delete file;
    return e;
  }

  ReadOptions ro;
  ro.fill_cache = false;
  Iterator* iter = table->NewIterator(ro);
  std::string r;
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    r.clear();
    ParsedInternalKey key;
    if (!ParseInternalKey(iter->key(), &key)) {
      r = "badkey '";
      AppendEscapedStringTo(&r, iter->key());
      r += "' => '";
      AppendEscapedStringTo(&r, iter->value());
      r += "'\n";
      dst->Append(r);
    } else {
      r = "'";
      AppendEscapedStringTo(&r, key.user_key);
      r += "' @ ";
      AppendNumberTo(&r, key.sequence);
      r += " : ";
      if (key.type == kTypeDeletion) {
        r += "del";
      } else if (key.type == kTypeValue) {
        r += "val";
      } else {
        AppendNumberTo(&r, key.type);
      }
      r += " => '";
      AppendEscapedStringTo(&r, iter->value());
      r += "'\n";
      dst->Append(r);
    }
  }
  e = iter->error();
  if (!e.ok()) {
    dst->Append("iterator error: " + e.ToString() + "\n");
  }

  delete iter;
  delete table;
  delete file;
  return Error(Error::Code::Ok);
}

}  // namespace

Error DumpFile(Env* env, const std::string& fname, WritableFile* dst) {
  FileType ftype;
  if (!GuessType(fname, &ftype)) {
    return Error(Error::Code::InvalidArgument, fname + ": unknown file type");
  }
  switch (ftype) {
    case kLogFile:
      return DumpLog(env, fname, dst);
    case kDescriptorFile:
      return DumpDescriptor(env, fname, dst);
    case kTableFile:
      return DumpTable(env, fname, dst);
    default:
      break;
  }
  return Error(Error::Code::InvalidArgument,
               fname + ": not a dump-able file type");
}

}  // namespace leveldb
