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

std::expected<WritableFile*, Error> Env::NewAppendableFile(
    const std::string& fname) {
  return std::unexpected(
      Error(Error::Code::NotSupported, "NewAppendableFile", fname));
}

std::optional<Error> Env::RemoveDir(const std::string& dirname) {
  return DeleteDir(dirname);
}
std::optional<Error> Env::DeleteDir(const std::string& dirname) {
  return RemoveDir(dirname);
}

std::optional<Error> Env::RemoveFile(const std::string& fname) {
  return DeleteFile(fname);
}
std::optional<Error> Env::DeleteFile(const std::string& fname) {
  return RemoveFile(fname);
}

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
  if (auto ret = env->NewWritableFile(fname)) {
    file = ret.value();
  } else {
    return std::move(ret.error());
  }

  Error err = file->Append(data).error_or(Error());
  if (err.ok() && should_sync) {
    err = file->Sync();
  }
  if (err.ok()) {
    err = file->Close().error_or(Error());
  }
  delete file;  // Will auto-close if we did not close above
  if (!err.ok()) {
    env->RemoveFile(fname);
  }
  return err;
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
  Error e;

  if (auto ret = env->NewSequentialFile(fname)) {
    file = ret.value();
  } else {
    return std::move(ret.error());
  }

  static const int kBufferSize = 8192;
  char* space = new char[kBufferSize];
  while (true) {
    std::string_view fragment;

    if (auto ret = file->Read(kBufferSize, space)) {
      fragment = ret.value();
    } else {
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
