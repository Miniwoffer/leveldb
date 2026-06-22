// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/coding.h"

#include <cstdint>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

namespace leveldb {

TEST(Coding, Fixed32) {
  std::string s;
  for (uint32_t v = 0; v < 100000; v++) {
    PutFixed(s, v);
  }

  const char* p = s.data();
  for (uint32_t v = 0; v < 100000; v++) {
    uint32_t actual = DecodeFixed32(p);
    ASSERT_EQ(v, actual);
    p += sizeof(uint32_t);
  }
}

TEST(Coding, Fixed64) {
  std::string s;
  for (int power = 0; power <= 63; power++) {
    uint64_t v = static_cast<uint64_t>(1) << power;
    PutFixed(s, v - 1);
    PutFixed(s, v + 0);
    PutFixed(s, v + 1);
  }

  const char* p = s.data();
  for (int power = 0; power <= 63; power++) {
    uint64_t v = static_cast<uint64_t>(1) << power;
    uint64_t actual;
    actual = DecodeFixed64(p);
    ASSERT_EQ(v - 1, actual);
    p += sizeof(uint64_t);

    actual = DecodeFixed64(p);
    ASSERT_EQ(v + 0, actual);
    p += sizeof(uint64_t);

    actual = DecodeFixed64(p);
    ASSERT_EQ(v + 1, actual);
    p += sizeof(uint64_t);
  }
}

// Test that encoding routines generate little-endian encodings
TEST(Coding, EncodingOutput) {
  std::string dst;
  PutFixed<uint32_t>(dst, 0x04030201);
  ASSERT_EQ(4, dst.size());
  ASSERT_EQ(0x01, static_cast<int>(dst[0]));
  ASSERT_EQ(0x02, static_cast<int>(dst[1]));
  ASSERT_EQ(0x03, static_cast<int>(dst[2]));
  ASSERT_EQ(0x04, static_cast<int>(dst[3]));

  dst.clear();
  PutFixed<uint64_t>(dst, 0x0807060504030201ull);
  ASSERT_EQ(8, dst.size());
  ASSERT_EQ(0x01, static_cast<int>(dst[0]));
  ASSERT_EQ(0x02, static_cast<int>(dst[1]));
  ASSERT_EQ(0x03, static_cast<int>(dst[2]));
  ASSERT_EQ(0x04, static_cast<int>(dst[3]));
  ASSERT_EQ(0x05, static_cast<int>(dst[4]));
  ASSERT_EQ(0x06, static_cast<int>(dst[5]));
  ASSERT_EQ(0x07, static_cast<int>(dst[6]));
  ASSERT_EQ(0x08, static_cast<int>(dst[7]));
}

TEST(Coding, Varint32) {
  std::string s;
  for (uint32_t i = 0; i < (32 * 32); i++) {
    uint32_t v = (i / 32) << (i % 32);
    PutVarint<uint32_t>(s, v);
  }

  std::string_view sv(s);

  for (uint32_t i = 0; i < (32 * 32); i++) {
    uint32_t expected = (i / 32) << (i % 32);
    auto resp = GetVarint<uint32_t>(sv);
    ASSERT_TRUE(resp);
    ASSERT_EQ(expected, resp->value);
    ASSERT_EQ(VarintLength(resp->value),
              sv.length() - resp->remaining_input.length());
    sv = resp->remaining_input;
  }
  ASSERT_TRUE(sv.empty());
}

TEST(Coding, Varint64) {
  // Construct the list of values to check
  std::vector<uint64_t> values;
  // Some special values
  values.push_back(0);
  values.push_back(100);
  values.push_back(~static_cast<uint64_t>(0));
  values.push_back(~static_cast<uint64_t>(0) - 1);
  for (uint32_t k = 0; k < 64; k++) {
    // Test values near powers of two
    const uint64_t power = 1ull << k;
    values.push_back(power);
    values.push_back(power - 1);
    values.push_back(power + 1);
  }

  std::string s;
  for (size_t i = 0; i < values.size(); i++) {
    PutVarint<uint64_t>(s, values[i]);
  }

  const char* p = s.data();
  const char* limit = p + s.size();
  for (size_t i = 0; i < values.size(); i++) {
    ASSERT_TRUE(p < limit);
    const char* start = p;
    auto resp = GetVarint<uint64_t>(std::string_view(p, limit));
    ASSERT_TRUE(resp);
    p = resp->remaining_input.data();
    ASSERT_EQ(values[i], resp->value);
    ASSERT_EQ(VarintLength(resp->value), resp->remaining_input.data() - start);
  }
  ASSERT_EQ(p, limit);
}

TEST(Coding, Varint32Overflow) {
  uint32_t result;
  std::string input("\x81\x82\x83\x84\x85\x11");
  ASSERT_FALSE(GetVarint<uint32_t>(input));
}

TEST(Coding, Varint32Truncation) {
  uint32_t large_value = (1u << 31) + 100;
  std::string s;
  PutVarint<uint32_t>(s, large_value);
  for (size_t len = 0; len < s.size() - 1; len++) {
    ASSERT_FALSE(GetVarint<uint32_t>(std::string_view(s.data(), len)));
  }
  auto resp = GetVarint<uint32_t>(s);
  ASSERT_TRUE(resp);
  ASSERT_EQ(large_value, resp->value);
}

TEST(Coding, Varint64Overflow) {
  uint64_t result;
  std::string input("\x81\x82\x83\x84\x85\x81\x82\x83\x84\x85\x11");
  ASSERT_FALSE(GetVarint<uint64_t>(input));
}

TEST(Coding, Varint64Truncation) {
  uint64_t large_value = (1ull << 63) + 100ull;
  std::string s;
  PutVarint<uint64_t>(s, large_value);
  for (size_t len = 0; len < s.size() - 1; len++) {
    ASSERT_FALSE(GetVarint<uint64_t>(std::string_view(s.data(), len)));
  }
  auto resp = GetVarint<uint64_t>(s);
  ASSERT_TRUE(resp);
  ASSERT_EQ(large_value, resp->value);
}

TEST(Coding, Strings) {
  std::string s;
  PutLengthPrefixedBlob<uint32_t>(s, "");
  PutLengthPrefixedBlob<uint32_t>(s, "foo");
  PutLengthPrefixedBlob<uint32_t>(s, "bar");
  PutLengthPrefixedBlob<uint32_t>(s, std::string(200, 'x'));

  std::string_view input(s);
  auto v = GetLengthPrefixedBlob<uint32_t>(input);
  ASSERT_TRUE(v);
  ASSERT_EQ("", v->value);
  v = GetLengthPrefixedBlob<uint32_t>(v->remaining_input);
  ASSERT_TRUE(v);
  ASSERT_EQ("foo", v->value);
  v = GetLengthPrefixedBlob<uint32_t>(v->remaining_input);
  ASSERT_TRUE(v);
  ASSERT_EQ("bar", v->value);
  v = GetLengthPrefixedBlob<uint32_t>(v->remaining_input);
  ASSERT_TRUE(v);
  ASSERT_EQ(std::string(200, 'x'), v->value);
  ASSERT_TRUE(v->remaining_input.empty());
}

}  // namespace leveldb
