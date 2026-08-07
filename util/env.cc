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

std::expected<void, Error> Env::RemoveDir(const std::string& dirname) {
  return DeleteDir(dirname);
}
std::expected<void, Error> Env::DeleteDir(const std::string& dirname) {
  return RemoveDir(dirname);
}

std::expected<void, Error> Env::RemoveFile(const std::string& fname) {
  return DeleteFile(fname);
}
std::expected<void, Error> Env::DeleteFile(const std::string& fname) {
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

static std::expected<void, Error> DoWriteStringToFile(
    Env* env, const std::string_view& data, const std::string& fname,
    bool should_sync) {
  WritableFile* file;
  if (auto ret = env->NewWritableFile(fname)) {
    file = ret.value();
  } else {
    return std::unexpected(ret.error());
  }

  auto ret = file->Append(data);
  if (ret && should_sync) {
    ret = file->Sync();
  }
  if (ret) {
    ret = file->Close();
  }
  delete file;  // Will auto-close if we did not close above
  if (!ret) {
    env->RemoveFile(fname);
  }
  return ret;
}

std::expected<void, Error> WriteStringToFile(Env* env,
                                             const std::string_view& data,
                                             const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, false);
}

std::expected<void, Error> WriteStringToFileSync(Env* env,
                                                 const std::string_view& data,
                                                 const std::string& fname) {
  return DoWriteStringToFile(env, data, fname, true);
}

std::expected<void, Error> ReadFileToString(Env* env, const std::string& fname,
                                            std::string* data) {
  data->clear();
  SequentialFile* file;

  if (auto ret = env->NewSequentialFile(fname)) {
    file = ret.value();
  } else {
    return std::unexpected(ret.error());
  }

  std::expected<void, Error> result{};
  static const int kBufferSize = 8192;
  char* space = new char[kBufferSize];
  while (true) {
    std::string_view fragment;

    if (auto ret = file->Read(kBufferSize, space)) {
      fragment = ret.value();
    } else {
      result = std::unexpected(ret.error());
    }

    data->append(fragment.data(), fragment.size());
    if (fragment.empty()) {
      break;
    }
  }
  delete[] space;
  delete file;
  return result;
}

EnvWrapper::~EnvWrapper() {}

}  // namespace leveldb
