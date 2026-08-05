// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "helpers/memenv/memenv.h"

#include "db/db_impl.h"
#include <string>
#include <vector>

#include "leveldb/db.h"
#include "leveldb/env.h"

#include "util/testutil.h"

#include "gtest/gtest.h"

namespace leveldb {

class MemEnvTest : public testing::Test {
 public:
  MemEnvTest() : env_(NewMemEnv(Env::Default())) {}
  ~MemEnvTest() { delete env_; }

  Env* env_;
};

TEST_F(MemEnvTest, Basics) {
  uint64_t file_size;
  WritableFile* writable_file;
  std::vector<std::string> children;

  ASSERT_TRUE(env_->CreateDir("/dir"));

  // Check that the directory is empty.
  ASSERT_FALSE(env_->FileExists("/dir/non_existent"));
  ASSERT_FALSE(env_->GetFileSize("/dir/non_existent"));
  auto ch_ret = env_->GetChildren("/dir");
  ASSERT_TRUE(ch_ret);
  children = std::move(ch_ret.value());
  ASSERT_EQ(0, children.size());

  // Create a file.
  auto wf_ret = env_->NewWritableFile("/dir/f");
  ASSERT_TRUE(wf_ret);
  writable_file = wf_ret.value();
  auto fs_ret = env_->GetFileSize("/dir/f");
  ASSERT_TRUE(fs_ret);
  file_size = fs_ret.value();
  ASSERT_EQ(0, file_size);
  delete writable_file;

  // Check that the file exists.
  ASSERT_TRUE(env_->FileExists("/dir/f"));
  fs_ret = env_->GetFileSize("/dir/f");
  ASSERT_TRUE(fs_ret);
  file_size = fs_ret.value();
  ASSERT_EQ(0, file_size);
  ch_ret = env_->GetChildren("/dir");
  ASSERT_TRUE(ch_ret);
  children = std::move(ch_ret.value());
  ASSERT_EQ(1, children.size());
  ASSERT_EQ("f", children[0]);

  // Write to the file.
  wf_ret = env_->NewWritableFile("/dir/f");
  ASSERT_TRUE(wf_ret);
  writable_file = wf_ret.value();
  ASSERT_TRUE(writable_file->Append("abc"));
  delete writable_file;

  // Check that append works.
  auto af_ret = env_->NewAppendableFile("/dir/f");
  ASSERT_TRUE(af_ret);
  writable_file = af_ret.value();
  fs_ret = env_->GetFileSize("/dir/f");
  ASSERT_TRUE(fs_ret);
  file_size = fs_ret.value();
  ASSERT_EQ(3, file_size);
  ASSERT_TRUE(writable_file->Append("hello"));
  delete writable_file;

  // Check for expected size.
  fs_ret = env_->GetFileSize("/dir/f");
  ASSERT_TRUE(fs_ret);
  file_size = fs_ret.value();
  ASSERT_EQ(8, file_size);

  // Check that renaming works.
  ASSERT_FALSE(env_->RenameFile("/dir/non_existent", "/dir/g"));
  ASSERT_FALSE(env_->RenameFile("/dir/f", "/dir/g").has_value());
  ASSERT_TRUE(!env_->FileExists("/dir/f"));
  ASSERT_TRUE(env_->FileExists("/dir/g"));
  fs_ret = env_->GetFileSize("/dir/f");
  ASSERT_TRUE(fs_ret);
  file_size = fs_ret.value();
  ASSERT_EQ(8, file_size);

  // Check that opening non-existent file fails.
  ASSERT_FALSE(env_->NewSequentialFile("/dir/non_existent"));
  ASSERT_FALSE(env_->NewRandomAccessFile("/dir/non_existent"));

  // Check that deleting works.
  ASSERT_FALSE(env_->RemoveFile("/dir/non_existent"));
  ASSERT_TRUE(env_->RemoveFile("/dir/g"));
  ASSERT_FALSE(env_->FileExists("/dir/g"));
  ch_ret = env_->GetChildren("/dir");
  ASSERT_TRUE(ch_ret);
  children = std::move(ch_ret.value());
  ASSERT_EQ(0, children.size());
  ASSERT_TRUE(env_->RemoveDir("/dir"));
}

TEST_F(MemEnvTest, ReadWrite) {
  WritableFile* writable_file;
  SequentialFile* seq_file;
  RandomAccessFile* rand_file;
  std::string_view result;
  char scratch[100];

  ASSERT_TRUE(env_->CreateDir("/dir"));

  auto wr_ret = env_->NewWritableFile("/dir/f");
  ASSERT_TRUE(wr_ret);
  writable_file = wr_ret.value();
  ASSERT_TRUE(writable_file->Append("hello "));
  ASSERT_TRUE(writable_file->Append("world"));
  delete writable_file;

  // Read sequentially.
  std::expected<SequentialFile*, Error> seq_ret;
  ASSERT_TRUE(seq_ret = env_->NewSequentialFile("/dir/f"));
  seq_file = seq_ret.value();
  auto rd_ret = seq_file->Read(5, scratch);
  ASSERT_TRUE(rd_ret);  // Read "hello".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("hello"));
  ASSERT_FALSE(seq_file->Skip(1));
  rd_ret = seq_file->Read(1000, scratch);
  ASSERT_TRUE(rd_ret);  // Read "world".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("world"));
  rd_ret = seq_file->Read(1000, scratch);
  ASSERT_TRUE(rd_ret);  // Try reading past EOF.
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.size());
  ASSERT_TRUE(seq_file->Skip(100));  // Try to skip past end of file.
  rd_ret = seq_file->Read(1000, scratch);
  ASSERT_TRUE(rd_ret);
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.size());
  delete seq_file;

  // Random reads.
  std::expected<RandomAccessFile*, Error> rand_ret;
  ASSERT_TRUE(rand_ret = env_->NewRandomAccessFile("/dir/f"));
  rand_file = rand_ret.value();
  rd_ret = rand_file->Read(6, 5, scratch);
  ASSERT_TRUE(rd_ret);  // Read "world".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("world"));
  ASSERT_TRUE(rand_file->Read(0, 5, scratch));  // Read "hello".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("hello"));
  ASSERT_TRUE(rand_file->Read(10, 100, scratch));  // Read "d".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("d"));

  // Too high offset.
  ASSERT_FALSE(rand_file->Read(1000, 5, scratch));
  delete rand_file;
}

TEST_F(MemEnvTest, Locks) {
  FileLock* lock;

  // These are no-ops, but we test they return success.
  auto ret = env_->LockFile("some file");
  ASSERT_TRUE(ret);
  lock = std::move(ret.value());
  ASSERT_TRUE(env_->UnlockFile(lock));
}

TEST_F(MemEnvTest, Misc) {
  std::string test_dir;
  auto td_ret = env_->GetTestDirectory();
  ASSERT_TRUE(td_ret);
  test_dir = std::move(td_ret.value());
  ASSERT_TRUE(!test_dir.empty());

  WritableFile* writable_file;
  auto ret = env_->NewWritableFile("/a/b");
  ASSERT_TRUE(ret);
  writable_file = std::move(ret.value());

  // These are no-ops, but we test they return success.
  ASSERT_TRUE(writable_file->Sync());
  ASSERT_TRUE(writable_file->Flush());
  ASSERT_TRUE(writable_file->Close());
  delete writable_file;
}

TEST_F(MemEnvTest, LargeWrite) {
  const size_t kWriteSize = 300 * 1024;
  char* scratch = new char[kWriteSize * 2];

  std::string write_data;
  for (size_t i = 0; i < kWriteSize; ++i) {
    write_data.append(1, static_cast<char>(i));
  }

  WritableFile* writable_file;
  auto wr_ret = env_->NewWritableFile("/dir/f");
  ASSERT_TRUE(wr_ret);
  writable_file = wr_ret.value();
  ASSERT_TRUE(writable_file->Append("foo"));
  ASSERT_TRUE(writable_file->Append(write_data));
  delete writable_file;

  std::expected<SequentialFile*, Error> seq_ret;
  SequentialFile* seq_file;
  std::string_view result;
  ASSERT_TRUE(seq_ret = env_->NewSequentialFile("/dir/f"));
  seq_file = seq_ret.value();
  auto rd_ret = seq_file->Read(3, scratch);
  ASSERT_TRUE(rd_ret);  // Read "foo".
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare("foo"));

  size_t read = 0;
  std::string read_data;
  while (read < kWriteSize) {
    rd_ret = seq_file->Read(kWriteSize - read, scratch);
    ASSERT_TRUE(rd_ret);
    result = std::move(rd_ret.value());
    read_data.append(result.data(), result.size());
    read += result.size();
  }
  ASSERT_TRUE(write_data == read_data);
  delete seq_file;
  delete[] scratch;
}

TEST_F(MemEnvTest, OverwriteOpenFile) {
  const char kWrite1Data[] = "Write #1 data";
  const size_t kFileDataLen = sizeof(kWrite1Data) - 1;
  const std::string kTestFileName = testing::TempDir() + "leveldb-TestFile.dat";

  ASSERT_LEVELDB_OK(WriteStringToFile(env_, kWrite1Data, kTestFileName));

  RandomAccessFile* rand_file;
  std::expected<RandomAccessFile*, Error> ret;
  ASSERT_TRUE(ret = env_->NewRandomAccessFile(kTestFileName));
  rand_file = std::move(ret.value());

  const char kWrite2Data[] = "Write #2 data";
  ASSERT_LEVELDB_OK(WriteStringToFile(env_, kWrite2Data, kTestFileName));

  // Verify that overwriting an open file will result in the new file data
  // being read from files opened before the write.
  std::string_view result;
  char scratch[kFileDataLen];
  auto rd_ret = rand_file->Read(0, kFileDataLen, scratch);
  ASSERT_TRUE(rd_ret);
  result = std::move(rd_ret.value());
  ASSERT_EQ(0, result.compare(kWrite2Data));

  delete rand_file;
}

TEST_F(MemEnvTest, DBTest) {
  Options options;
  options.create_if_missing = true;
  options.env = env_;
  std::shared_ptr<DB> db;

  const std::string_view keys[] = {{"aaa"}, {"bbb"}, {"ccc"}};
  const std::string_view vals[] = {{"foo"}, {"bar"}, {"baz"}};
  auto res = DB::Open(options, "/dir/db");
  ASSERT_TRUE(res);
  db = res.value();
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(db->Put(WriteOptions(), keys[i], vals[i]));
  }

  for (size_t i = 0; i < 3; ++i) {
    auto res = db->Get(ReadOptions(), keys[i]);
    ASSERT_TRUE(res);
    ASSERT_TRUE(*res == vals[i]);
  }

  {
    auto iterator = db->NewIterator(ReadOptions());
    iterator->SeekToFirst();
    for (size_t i = 0; i < 3; ++i) {
      ASSERT_TRUE(iterator->Valid());
      ASSERT_TRUE(keys[i] == iterator->key());
      ASSERT_TRUE(vals[i] == iterator->value());
      iterator->Next();
    }
    ASSERT_TRUE(!iterator->Valid());
  }

  std::shared_ptr<DBImpl> dbi = std::static_pointer_cast<DBImpl>(db);
  ASSERT_LEVELDB_OK(dbi->TEST_CompactMemTable());

  for (size_t i = 0; i < 3; ++i) {
    auto res = db->Get(ReadOptions(), keys[i]);
    ASSERT_TRUE(res);
    ASSERT_TRUE(*res == vals[i]);
  }
}

}  // namespace leveldb
