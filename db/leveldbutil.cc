// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <cstdio>

#include "leveldb/dumpfile.h"
#include "leveldb/env.h"
#include "leveldb/status.h"

namespace leveldb {
namespace {

class StdoutPrinter : public WritableFile {
 public:
  Error Append(const std::string_view& data) override {
    fwrite(data.data(), 1, data.size(), stdout);
    return Error::OK();
  }
  Error Close() override { return Error::OK(); }
  Error Flush() override { return Error::OK(); }
  Error Sync() override { return Error::OK(); }
};

bool HandleDumpCommand(Env* env, char** files, int num) {
  StdoutPrinter printer;
  bool ok = true;
  for (int i = 0; i < num; i++) {
    Error e = DumpFile(env, files[i], &printer);
    if (!e.ok()) {
      std::fprintf(stderr, "%s\n", e.ToString().c_str());
      ok = false;
    }
  }
  return ok;
}

}  // namespace
}  // namespace leveldb

static void Usage() {
  std::fprintf(
      stderr,
      "Usage: leveldbutil command...\n"
      "   dump files...         -- dump contents of specified files\n");
}

int main(int argc, char** argv) {
  leveldb::Env* env = leveldb::Env::Default();
  bool ok = true;
  if (argc < 2) {
    Usage();
    ok = false;
  } else {
    std::string command = argv[1];
    if (command == "dump") {
      ok = leveldb::HandleDumpCommand(env, argv + 2, argc - 2);
    } else {
      Usage();
      ok = false;
    }
  }
  return (ok ? 0 : 1);
}
