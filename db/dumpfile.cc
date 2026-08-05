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
#include <format>
#include <variant>

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
  if (auto ret = env->NewSequentialFile(fname)) {
    file = ret.value();
  } else {
    return ret.error();
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

  std::expected<void, Error> status = {};
  for (auto entry : WriteBatch::Range(batch, status)) {
    std::visit(
        overloaded{
            [dst](WriteBatch::PutEntry& e) {
              dst->Append(std::format("  put '{}' '{}'\n", e.key, e.value));
            },
            [dst](WriteBatch::DeleteEntry& e) {
              dst->Append(std::format("  del '{}'\n", e.key));
            },
        },
        entry);
  }
  if (!status) {
    dst->Append("  error: " + status.error().ToString() + "\n");
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
  Error err;

  if (auto fs_ret = env->GetFileSize(fname)) {
    file_size = fs_ret.value();
    if (auto rf_ret = env->NewRandomAccessFile(fname)) {
      file = rf_ret.value();
    } else {
      err = std::move(rf_ret.error());
    }
  } else {
    err = std::move(fs_ret.error());
  }

  if (err.ok()) {
    // We use the default comparator, which may or may not match the
    // comparator used in this database. However this should not cause
    // problems since we only use Table operations that do not require
    // any comparisons.  In particular, we do not call Seek or Prev.
    err = Table::Open(Options(), file, file_size, &table);
  }

  if (!err.ok()) {
    delete table;
    delete file;
    return err;
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
  err = iter->error();
  if (!err.ok()) {
    dst->Append("iterator error: " + err.ToString() + "\n");
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
