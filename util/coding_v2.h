// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Endian-neutral encoding:
// * Fixed-length numbers are encoded with least-significant byte first
// * In addition we support variable length "varint" encoding
// * Strings are encoded prefixed by their length in varint format

#ifndef STORAGE_LEVELDB_UTIL_CODING_V2_H_
#define STORAGE_LEVELDB_UTIL_CODING_V2_H_

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <regex.h>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace leveldb {

template <typename T>
concept is_unsigned_integral = std::is_integral_v<T> && std::is_unsigned_v<T>;

// Decode
template <typename T>
struct DecodingResult {
  T value;
  std::string_view remaining_input;
};
template <typename T>
  requires is_unsigned_integral<T>
inline std::optional<DecodingResult<T>> GetVarint(std::string_view input) {
  T result = 0;
  size_t shift = 0;

  // Max bits for type T, 7 bits per iteration
  for (; !input.empty() && shift < sizeof(T) * 8; shift += 7) {
    uint8_t byte = input.front();
    input.remove_prefix(1);

    result |= static_cast<T>(byte & 127) << shift;

    if (!(byte & 128)) {
      return DecodingResult<T>{result, input};
    }
  }

  return std::nullopt;
}

template <typename T, typename E>
  requires is_unsigned_integral<T>
inline std::span<E> EncodeVarint(std::span<E> input, T value) {
  static const size_t B = 128;
  size_t written = 0;
  uint8_t* ptr = reinterpret_cast<uint8_t*>(input.data());

  while (value >= B) {
    ptr[written++] = static_cast<uint8_t>(value | B);
    value >>= 7;
  }
  ptr[written++] = value;
  return input.subspan(written);
}
template <typename T>
  requires is_unsigned_integral<T>
inline void PutVarint(std::string& dst, T v) {
  constexpr const size_t max_size = sizeof(T) * 8 / 7 + 1;
  uint8_t uint8_buf[max_size];
  std::span<uint8_t> buf(uint8_buf, max_size);
  auto resp = EncodeVarint<T>(buf, v);
  dst.append(buf.begin(), resp.begin());
}

template <typename T>
  requires is_unsigned_integral<T>
inline void PutLengthPrefixedString(std::string& dst, std::string_view v) {
  PutVarint<T>(dst, (T)v.size());
  dst.append(v);
}

template <typename T, typename E>
  requires is_unsigned_integral<T>
inline std::span<E> EncodeLengthPrefixedString(std::span<E> dst,
                                               std::string_view v) {
  dst = EncodeVarint<T>(dst, (T)v.size());
  memcpy(dst.data(), v.data(), v.size());
  return dst.subspan(v.size());
}

template <typename T>
  requires std::is_integral_v<T>
inline T DecodeFixed(std::string_view data) {
  T value;
  std::memcpy(&value, data.data(), sizeof(T));

  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(value);
  }

  return value;
}

template <typename T, typename E>
  requires std::is_integral_v<T>
inline std::span<E> EncodeFixed(std::span<E> data, T value) {
  std::memcpy(data.data(), &value, sizeof(T));
  if constexpr (std::endian::native == std::endian::big) {
    std::byteswap((T*)(data.data()));
  }
  return data.subspan(sizeof(T));
}

template <typename T>
  requires std::is_integral_v<T>
inline void PutFixed(std::string& data, T value) {
  char buff[sizeof(T)];
  std::memcpy(buff, &value, sizeof(T));
  if constexpr (std::endian::native == std::endian::big) {
    std::byteswap(buff);
  }
  data.append(buff, sizeof(T));
}

template <typename T>
  requires is_unsigned_integral<T>
std::optional<DecodingResult<std::string_view>> GetLengthPrefixedData(
    std::string_view input) {
  if (auto len = GetVarint<T>(input);
      len && len->remaining_input.size() >= len->value) {
    std::string_view ret(len->remaining_input.data(), len->value);
    len->remaining_input.remove_prefix(len->value);
    return DecodingResult{ret, len->remaining_input};
  } else {
    return {};
  }
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_CODING_V2_H_
