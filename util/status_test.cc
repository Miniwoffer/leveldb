// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/status.h"

#include <utility>

#include "leveldb/error.h"

#include "gtest/gtest.h"

namespace leveldb {

TEST(Status, MoveConstructor) {
  {
    Error e(Error::Code::IOError, "foo");
    Error e2(Error::Code::NotFound);
    ASSERT_EQ(sizeof(e), sizeof(std::unique_ptr<std::string>));
    ASSERT_EQ("IO error: foo", e.ToString());
    ASSERT_EQ("Not found", e2.ToString());

    Status ok = Status::OK();
    Status ok2 = std::move(ok);

    ASSERT_TRUE(ok2.ok());
  }

  {
    Status status = Status::NotFound("custom Not found status message");
    Status status2 = std::move(status);

    ASSERT_TRUE(status2.IsNotFound());
    ASSERT_EQ("Not found: custom Not found status message", status2.ToString());
  }

  {
    Status self_moved = Status::IOError("custom IOError status message");

    // Needed to bypass compiler warning about explicit move-assignment.
    Status& self_moved_reference = self_moved;
    self_moved_reference = std::move(self_moved);
  }
}

}  // namespace leveldb
