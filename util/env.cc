// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/env.h"

#include <cstdarg>

// This workaround can be removed when leveldb::Env::DeleteFile is removed.
// See env.h for justification.
#if defined(_WIN32) && defined(LEVELDB_DELETEFILE_UNDEFINED)
#undef DeleteFile
#endif

namespace leveldb {

Env::Env() = default;

Env::~Env() = default;

Error Env::NewAppendableFile(const std::string& fname, WritableFile** result) {
  return Error(Error::Code::NotSupported, "NewAppendableFile", fname);
}

Error Env::RemoveDir(const std::string& dirname) { return DeleteDir(dirname); }
Error Env::DeleteDir(const std::string& dirname) { return RemoveDir(dirname); }

Error Env::RemoveFile(const std::string& fname) { return DeleteFile(fname); }
Error Env::DeleteFile(const std::string& fname) { return RemoveFile(fname); }

SequentialFile::~SequentialFile() = default;

RandomAccessFile::~RandomAccessFile() = default;

WritableFile::~WritableFile() = default;

Logger::~Logger() = default;

FileLock::~FileLock() = default;

void Log(Logger* info_log, const char* format, ...) {
  if (info_log != nullptr) {
    std::va_list ap;
    va_start(ap, format);
    info_log->Logv(format, ap);
    va_end(ap);
  }
}

static Error DoWriteStringToFile(Env* env, const std::string_view& data,
                                 const std::string& fname, bool should_sync) {
  WritableFile* file;
  Error e = env->NewWritableFile(fname, &file);
  if (!e.ok()) {
    return e;
  }
  e = file->Append(data);
  if (e.ok() && should_sync) {
    e = file->Sync();
  }
  if (e.ok()) {
    e = file->Close();
  }
  delete file;  // Will auto-close if we did not close above
  if (!e.ok()) {
    env->RemoveFile(fname);
  }
  return e;
}

Error WriteStringToFile(Env* env, const std::string_view& data,
                        const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, false);
}

Error WriteStringToFileSync(Env* env, const std::string_view& data,
                            const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, true);
}

Error ReadFileToString(Env* env, const std::string& fname, std::string* data) {
  data->clear();
  SequentialFile* file;
  Error e = env->NewSequentialFile(fname, &file);
  if (!e.ok()) {
    return e;
  }
  static const int kBufferSize = 8192;
  char* space = new char[kBufferSize];
  while (true) {
    std::string_view fragment;
    e = file->Read(kBufferSize, &fragment, space);
    if (!e.ok()) {
      break;
    }
    data->append(fragment.data(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  delete[] space;
  delete file;
  return e;
}

EnvWrapper::~EnvWrapper() {}

}  // namespace leveldb
