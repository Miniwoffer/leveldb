// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/error.h"

#include <utility>

#include "gtest/gtest.h"

namespace leveldb {

Error foo() {
  Error e(Error::Code::NotFound);
  return e;
}

TEST(Error, MoveConstructor) {
  {
    Error e(Error::Code::InvalidArgument);
    Error e2 = foo();

    Error ok = Error(Error::Code::Ok);
    Error ok2 = std::move(ok);

    ASSERT_TRUE(ok2.ok());
  }

  {
    Error err = Error(Error::Code::NotFound, "custom Not found error message");
    Error err2 = std::move(err);

    ASSERT_TRUE(err2.IsNotFound());
    ASSERT_EQ("Not found: custom Not found error message", err2.ToString());
  }

  {
    Error self_moved =
        Error(Error::Code::IOError, "custom IOError error message");

    // Needed to bypass compiler warning about explicit move-assignment.
    Error& self_moved_reference = self_moved;
    self_moved_reference = std::move(self_moved);
  }
}

}  // namespace leveldb
