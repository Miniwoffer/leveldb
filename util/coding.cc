// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/coding.h"

#include <cstdint>
#include <optional>

#include "leveldb/slice.h"

namespace leveldb {

void PutFixed32(std::string* dst, uint32_t value) {
  char buf[sizeof(value)];
  EncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutFixed64(std::string* dst, uint64_t value) {
  char buf[sizeof(value)];
  EncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

char* EncodeVarint32(char* dst, uint32_t v) {
  return reinterpret_cast<char*>(
      EncodeVarint(std::span(reinterpret_cast<uint8_t*>(dst), 5), v).data());
}

void PutVarint32(std::string* dst, uint32_t v) {
  uint8_t buf[5];
  auto resp = EncodeVarint(std::span(buf, 5), v);
  dst->append(reinterpret_cast<char*>(buf), sizeof(buf) - resp.size());
}

char* EncodeVarint64(char* dst, uint64_t v) {
  return reinterpret_cast<char*>(
      EncodeVarint(std::span((uint8_t*)dst, 10), v).data());
}

void PutVarint64(std::string* dst, uint64_t v) {
  uint8_t buf[10];
  auto resp = EncodeVarint(std::span(buf, 10), v);
  dst->append(reinterpret_cast<char*>(buf), sizeof(buf) - resp.size());
}

void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}

int VarintLength(uint64_t v) {
  int len = 1;
  while (v >= 128) {
    v >>= 7;
    len++;
  }
  return len;
}

const char* GetVarint32PtrFallback(const char* p, const char* limit,
                                   uint32_t* value) {
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
    uint32_t byte = *(reinterpret_cast<const uint8_t*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return nullptr;
}

std::optional<uint32_t> GetVarint32PtrFallback(Slice& data) {
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && !data.empty(); shift += 7) {
    uint32_t byte = *(reinterpret_cast<const uint8_t*>(data.data()));
    data.remove_prefix(1);
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      return result;
    }
  }
  return {};
}

bool GetVarint32(Slice* input, uint32_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint32Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

std::optional<uint32_t> GetVarint32(Slice& input) {
  Slice data(input);
  auto q = GetVarint32Ptr(data);
  if (q) {
    input = data;
  }
  return q;
}

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
  uint64_t result = 0;
  for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
    uint64_t byte = *(reinterpret_cast<const uint8_t*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return nullptr;
}

bool GetVarint64(Slice* input, uint64_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint64Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

std::optional<Slice> GetLengthPrefixedSlice(Slice& input) {
  if (std::optional<uint32_t> len = GetVarint32(input);
      len && input.size() >= *len) {
    Slice ret(input.data(), *len);
    input.remove_prefix(*len);
    return ret;
  } else {
    return {};
  }
}

}  // namespace leveldb
