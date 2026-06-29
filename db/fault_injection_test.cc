// Copyright 2014 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// This test uses a custom Env to keep track of the state of a filesystem as of
// the last "sync". It then checks for data loss errors by purposely dropping
// file data (or entire files) not protected by a "sync".

#include "db/db_impl.h"
#include "db/filename.h"
#include "db/log_format.h"
#include "db/version_set.h"
#include <map>
#include <set>

#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/table.h"
#include "leveldb/write_batch.h"

#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/testutil.h"

#include "gtest/gtest.h"

namespace leveldb {

static const int kValueSize = 1000;
static const int kMaxNumValues = 2000;
static const size_t kNumIterations = 3;

class FaultInjectionTestEnv;

namespace {

// Assume a filename, and not a directory name like "/foo/bar/"
static std::string GetDirName(const std::string& filename) {
  size_t found = filename.find_last_of("/\\");
  if (found == std::string::npos) {
    return "";
  } else {
    return filename.substr(0, found);
  }
}

Error SyncDir(const std::string& dir) {
  // As this is a test it isn't required to *actually* sync this directory.
  return Error::OK();
}

// A basic file truncation function suitable for this test.
Error Truncate(const std::string& filename, uint64_t length) {
  leveldb::Env* env = leveldb::Env::Default();

  SequentialFile* orig_file;
  Error e = env->NewSequentialFile(filename, &orig_file);
  if (!e.ok()) return e;

  char* scratch = new char[length];
  leveldb::std::string_view result;
  e = orig_file->Read(length, &result, scratch);
  delete orig_file;
  if (e.ok()) {
    std::string tmp_name = GetDirName(filename) + "/truncate.tmp";
    WritableFile* tmp_file;
    e = env->NewWritableFile(tmp_name, &tmp_file);
    if (e.ok()) {
      e = tmp_file->Append(result);
      delete tmp_file;
      if (e.ok()) {
        e = env->RenameFile(tmp_name, filename);
      } else {
        env->RemoveFile(tmp_name);
      }
    }
  }

  delete[] scratch;

  return e;
}

struct FileState {
  std::string filename_;
  int64_t pos_;
  int64_t pos_at_last_sync_;
  int64_t pos_at_last_flush_;

  FileState(const std::string& filename)
      : filename_(filename),
        pos_(-1),
        pos_at_last_sync_(-1),
        pos_at_last_flush_(-1) {}

  FileState() : pos_(-1), pos_at_last_sync_(-1), pos_at_last_flush_(-1) {}

  bool IsFullySynced() const { return pos_ <= 0 || pos_ == pos_at_last_sync_; }

  Error DropUnsyncedData() const;
};

}  // anonymous namespace

// A wrapper around WritableFile which informs another Env whenever this file
// is written to or sync'ed.
class TestWritableFile : public WritableFile {
 public:
  TestWritableFile(const FileState& state, WritableFile* f,
                   FaultInjectionTestEnv* env);
  ~TestWritableFile() override;
  Error Append(const std::string_view& data) override;
  Error Close() override;
  Error Flush() override;
  Error Sync() override;

 private:
  FileState state_;
  WritableFile* target_;
  bool writable_file_opened_;
  FaultInjectionTestEnv* env_;

  Error SyncParent();
};

class FaultInjectionTestEnv : public EnvWrapper {
 public:
  FaultInjectionTestEnv()
      : EnvWrapper(Env::Default()), filesystem_active_(true) {}
  ~FaultInjectionTestEnv() override = default;
  Error NewWritableFile(const std::string& fname,
                        WritableFile** result) override;
  Error NewAppendableFile(const std::string& fname,
                          WritableFile** result) override;
  Error RemoveFile(const std::string& f) override;
  Error RenameFile(const std::string& s, const std::string& t) override;

  void WritableFileClosed(const FileState& state);
  Error DropUnsyncedFileData();
  Error RemoveFilesCreatedAfterLastDirSync();
  void DirWasSynced();
  bool IsFileCreatedSinceLastDirSync(const std::string& filename);
  void ResetState();
  void UntrackFile(const std::string& f);
  // Setting the filesystem to inactive is the test equivalent to simulating a
  // system reset. Setting to inactive will freeze our saved filesystem state so
  // that it will stop being recorded. It can then be reset back to the state at
  // the time of the reset.
  bool IsFilesystemActive() LOCKS_EXCLUDED(mutex_) {
    MutexLock l(&mutex_);
    return filesystem_active_;
  }
  void SetFilesystemActive(bool active) LOCKS_EXCLUDED(mutex_) {
    MutexLock l(&mutex_);
    filesystem_active_ = active;
  }

 private:
  port::Mutex mutex_;
  std::map<std::string, FileState> db_file_state_ GUARDED_BY(mutex_);
  std::set<std::string> new_files_since_last_dir_sync_ GUARDED_BY(mutex_);
  bool filesystem_active_ GUARDED_BY(mutex_);  // Record flushes, syncs, writes
};

TestWritableFile::TestWritableFile(const FileState& state, WritableFile* f,
                                   FaultInjectionTestEnv* env)
    : state_(state), target_(f), writable_file_opened_(true), env_(env) {
  assert(f != nullptr);
}

TestWritableFile::~TestWritableFile() {
  if (writable_file_opened_) {
    Close();
  }
  delete target_;
}

Error TestWritableFile::Append(const std::string_view& data) {
  Error e = target_->Append(data);
  if (e.ok() && env_->IsFilesystemActive()) {
    state_.pos_ += data.size();
  }
  return e;
}

Error TestWritableFile::Close() {
  writable_file_opened_ = false;
  Error e = target_->Close();
  if (e.ok()) {
    env_->WritableFileClosed(state_);
  }
  return e;
}

Error TestWritableFile::Flush() {
  Error e = target_->Flush();
  if (e.ok() && env_->IsFilesystemActive()) {
    state_.pos_at_last_flush_ = state_.pos_;
  }
  return e;
}

Error TestWritableFile::SyncParent() {
  Error e = SyncDir(GetDirName(state_.filename_));
  if (e.ok()) {
    env_->DirWasSynced();
  }
  return e;
}

Error TestWritableFile::Sync() {
  if (!env_->IsFilesystemActive()) {
    return Error::OK();
  }
  // Ensure new files referred to by the manifest are in the filesystem.
  Error e = target_->Sync();
  if (e.ok()) {
    state_.pos_at_last_sync_ = state_.pos_;
  }
  if (env_->IsFileCreatedSinceLastDirSync(state_.filename_)) {
    Error ps = SyncParent();
    if (e.ok() && !ps.ok()) {
      e = ps;
    }
  }
  return e;
}

Error FaultInjectionTestEnv::NewWritableFile(const std::string& fname,
                                             WritableFile** result) {
  WritableFile* actual_writable_file;
  Error e = target()->NewWritableFile(fname, &actual_writable_file);
  if (e.ok()) {
    FileState state(fname);
    state.pos_ = 0;
    *result = new TestWritableFile(state, actual_writable_file, this);
    // NewWritableFile doesn't append to files, so if the same file is
    // opened again then it will be truncated - so forget our saved
    // state.
    UntrackFile(fname);
    MutexLock l(&mutex_);
    new_files_since_last_dir_sync_.insert(fname);
  }
  return e;
}

Error FaultInjectionTestEnv::NewAppendableFile(const std::string& fname,
                                               WritableFile** result) {
  WritableFile* actual_writable_file;
  Error e = target()->NewAppendableFile(fname, &actual_writable_file);
  if (e.ok()) {
    FileState state(fname);
    state.pos_ = 0;
    {
      MutexLock l(&mutex_);
      if (db_file_state_.count(fname) == 0) {
        new_files_since_last_dir_sync_.insert(fname);
      } else {
        state = db_file_state_[fname];
      }
    }
    *result = new TestWritableFile(state, actual_writable_file, this);
  }
  return e;
}

Error FaultInjectionTestEnv::DropUnsyncedFileData() {
  Error e;
  MutexLock l(&mutex_);
  for (const auto& kvp : db_file_state_) {
    if (!e.ok()) {
      break;
    }
    const FileState& state = kvp.second;
    if (!state.IsFullySynced()) {
      e = state.DropUnsyncedData();
    }
  }
  return e;
}

void FaultInjectionTestEnv::DirWasSynced() {
  MutexLock l(&mutex_);
  new_files_since_last_dir_sync_.clear();
}

bool FaultInjectionTestEnv::IsFileCreatedSinceLastDirSync(
    const std::string& filename) {
  MutexLock l(&mutex_);
  return new_files_since_last_dir_sync_.find(filename) !=
         new_files_since_last_dir_sync_.end();
}

void FaultInjectionTestEnv::UntrackFile(const std::string& f) {
  MutexLock l(&mutex_);
  db_file_state_.erase(f);
  new_files_since_last_dir_sync_.erase(f);
}

Error FaultInjectionTestEnv::RemoveFile(const std::string& f) {
  Error e = EnvWrapper::RemoveFile(f);
  EXPECT_LEVELDB_OK(e);
  if (e.ok()) {
    UntrackFile(f);
  }
  return e;
}

Error FaultInjectionTestEnv::RenameFile(const std::string& s,
                                        const std::string& t) {
  Error ret = EnvWrapper::RenameFile(s, t);

  if (ret.ok()) {
    MutexLock l(&mutex_);
    if (db_file_state_.find(s) != db_file_state_.end()) {
      db_file_state_[t] = db_file_state_[s];
      db_file_state_.erase(s);
    }

    if (new_files_since_last_dir_sync_.erase(s) != 0) {
      assert(new_files_since_last_dir_sync_.find(t) ==
             new_files_since_last_dir_sync_.end());
      new_files_since_last_dir_sync_.insert(t);
    }
  }

  return ret;
}

void FaultInjectionTestEnv::ResetState() {
  // Since we are not destroying the database, the existing files
  // should keep their recorded synced/flushed state. Therefore
  // we do not reset db_file_state_ and new_files_since_last_dir_sync_.
  SetFilesystemActive(true);
}

Error FaultInjectionTestEnv::RemoveFilesCreatedAfterLastDirSync() {
  // Because RemoveFile access this container make a copy to avoid deadlock
  mutex_.Lock();
  std::set<std::string> new_files(new_files_since_last_dir_sync_.begin(),
                                  new_files_since_last_dir_sync_.end());
  mutex_.Unlock();
  Error err;
  for (const auto& new_file : new_files) {
    Error remove_err = RemoveFile(new_file);
    if (!remove_err.ok() && err.ok()) {
      err = std::move(remove_err);
    }
  }
  return err;
}

void FaultInjectionTestEnv::WritableFileClosed(const FileState& state) {
  MutexLock l(&mutex_);
  db_file_state_[state.filename_] = state;
}

Error FileState::DropUnsyncedData() const {
  int64_t sync_pos = pos_at_last_sync_ == -1 ? 0 : pos_at_last_sync_;
  return Truncate(filename_, sync_pos);
}

class FaultInjectionTest : public testing::Test {
 public:
  enum ExpectedVerifResult { VAL_EXPECT_NO_ERROR, VAL_EXPECT_ERROR };
  enum ResetMethod { RESET_DROP_UNSYNCED_DATA, RESET_DELETE_UNSYNCED_FILES };

  FaultInjectionTestEnv* env_;
  std::string dbname_;
  Cache* tiny_cache_;
  Options options_;
  DB* db_;

  FaultInjectionTest()
      : env_(new FaultInjectionTestEnv),
        tiny_cache_(NewLRUCache(100)),
        db_(nullptr) {
    dbname_ = testing::TempDir() + "fault_test";
    DestroyDB(dbname_, Options());  // Destroy any db from earlier run
    options_.reuse_logs = true;
    options_.env = env_;
    options_.paranoid_checks = true;
    options_.block_cache = tiny_cache_;
    options_.create_if_missing = true;
  }

  ~FaultInjectionTest() {
    CloseDB();
    DestroyDB(dbname_, Options());
    delete tiny_cache_;
    delete env_;
  }

  void ReuseLogs(bool reuse) { options_.reuse_logs = reuse; }

  void Build(int start_idx, int num_vals) {
    std::string key_space, value_space;
    WriteBatch batch;
    for (int i = start_idx; i < start_idx + num_vals; i++) {
      std::string_view key = Key(i, &key_space);
      batch.Clear();
      batch.Put(key, Value(i, &value_space));
      WriteOptions options;
      ASSERT_LEVELDB_OK(db_->Write(options, &batch));
    }
  }

  Error ReadValue(int i, std::string* val) const {
    std::string key_space, value_space;
    std::string_view key = Key(i, &key_space);
    Value(i, &value_space);
    ReadOptions options;
    return db_->Get(options, key, val);
  }

  Error Verify(int start_idx, int num_vals,
               ExpectedVerifResult expected) const {
    std::string val;
    std::string value_space;
    Error e;
    for (int i = start_idx; i < start_idx + num_vals && e.ok(); i++) {
      Value(i, &value_space);
      e = ReadValue(i, &val);
      if (expected == VAL_EXPECT_NO_ERROR) {
        if (e.ok()) {
          EXPECT_EQ(value_space, val);
        }
      } else if (e.ok()) {
        std::fprintf(stderr, "Expected an error at %d, but was OK\n", i);
        e = Error::IOError(dbname_, "Expected value error:");
      } else {
        e = Error::OK();  // An expected error
      }
    }
    return e;
  }

  // Return the ith key
  std::string_view Key(int i, std::string* storage) const {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "%016d", i);
    storage->assign(buf, strlen(buf));
    return std::string_view(*storage);
  }

  // Return the value to associate with the specified key
  std::string_view Value(int k, std::string* storage) const {
    Random r(k);
    return test::RandomString(&r, kValueSize, storage);
  }

  Error OpenDB() {
    delete db_;
    db_ = nullptr;
    env_->ResetState();
    return DB::Open(options_, dbname_, &db_);
  }

  void CloseDB() {
    delete db_;
    db_ = nullptr;
  }

  void DeleteAllData() {
    Iterator* iter = db_->NewIterator(ReadOptions());
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      ASSERT_LEVELDB_OK(db_->Delete(WriteOptions(), iter->key()));
    }

    delete iter;
  }

  void ResetDBState(ResetMethod reset_method) {
    switch (reset_method) {
      case RESET_DROP_UNSYNCED_DATA:
        ASSERT_LEVELDB_OK(env_->DropUnsyncedFileData());
        break;
      case RESET_DELETE_UNSYNCED_FILES:
        ASSERT_LEVELDB_OK(env_->RemoveFilesCreatedAfterLastDirSync());
        break;
      default:
        assert(false);
    }
  }

  void PartialCompactTestPreFault(int num_pre_sync, int num_post_sync) {
    DeleteAllData();
    Build(0, num_pre_sync);
    db_->CompactRange(nullptr, nullptr);
    Build(num_pre_sync, num_post_sync);
  }

  void PartialCompactTestReopenWithFault(ResetMethod reset_method,
                                         int num_pre_sync, int num_post_sync) {
    env_->SetFilesystemActive(false);
    CloseDB();
    ResetDBState(reset_method);
    ASSERT_LEVELDB_OK(OpenDB());
    ASSERT_LEVELDB_OK(
        Verify(0, num_pre_sync, FaultInjectionTest::VAL_EXPECT_NO_ERROR));
    ASSERT_LEVELDB_OK(Verify(num_pre_sync, num_post_sync,
                             FaultInjectionTest::VAL_EXPECT_ERROR));
  }

  void NoWriteTestPreFault() {}

  void NoWriteTestReopenWithFault(ResetMethod reset_method) {
    CloseDB();
    ResetDBState(reset_method);
    ASSERT_LEVELDB_OK(OpenDB());
  }

  void DoTest() {
    Random rnd(0);
    ASSERT_LEVELDB_OK(OpenDB());
    for (size_t idx = 0; idx < kNumIterations; idx++) {
      int num_pre_sync = rnd.Uniform(kMaxNumValues);
      int num_post_sync = rnd.Uniform(kMaxNumValues);

      PartialCompactTestPreFault(num_pre_sync, num_post_sync);
      PartialCompactTestReopenWithFault(RESET_DROP_UNSYNCED_DATA, num_pre_sync,
                                        num_post_sync);

      NoWriteTestPreFault();
      NoWriteTestReopenWithFault(RESET_DROP_UNSYNCED_DATA);

      PartialCompactTestPreFault(num_pre_sync, num_post_sync);
      // No new files created so we expect all values since no files will be
      // dropped.
      PartialCompactTestReopenWithFault(RESET_DELETE_UNSYNCED_FILES,
                                        num_pre_sync + num_post_sync, 0);

      NoWriteTestPreFault();
      NoWriteTestReopenWithFault(RESET_DELETE_UNSYNCED_FILES);
    }
  }
};

TEST_F(FaultInjectionTest, FaultTestNoLogReuse) {
  ReuseLogs(false);
  DoTest();
}

TEST_F(FaultInjectionTest, FaultTestWithLogReuse) {
  ReuseLogs(true);
  DoTest();
}

}  // namespace leveldb
