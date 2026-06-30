#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <ranges>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

namespace {
static consteval auto CodeType_() {
  constexpr const size_t ptr_sz = sizeof(size_t);
  if constexpr (ptr_sz == 8) {
    return uint32_t{};
  } else if constexpr (ptr_sz == 4) {
    return uint16_t{};
  } else {
    return unsigned{};
  }
}
}  // namespace

class LEVELDB_EXPORT Error {
  using code_t = decltype(CodeType_());

 public:
  enum class Code : code_t {
    Ok = 0,  // TODO: deprecate after everything is expected returns semantics
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOFault = 5,
    Count_
  };

  Error() noexcept = default;  // TODO: deprecate after everything is expected
                               // returns semantics
  Error(Code c) noexcept : code_(c) {}
  ~Error();

  template <typename... Args>
    requires(std::convertible_to<Args, std::string_view> && ...)
  Error(Code c, Args... args) : code_(c), msg_key_(GetNextKey()) {
    std::string_view msgs_arr[] = {std::string_view(args)...};
    auto joined_view = msgs_arr | std::views::join_with(std::string_view(": "));
    std::string buffer;
    std::ranges::copy(joined_view, std::back_inserter(buffer));
    SetMessage(std::move(buffer));
  }

  Error(const Error&);
  Error& operator=(const Error&);

  Error(Error&&) noexcept;
  Error& operator=(Error&&) noexcept;

  bool operator==(const Code rhs) const { return code_ == rhs; }
  bool ok() const { return code_ == Code::Ok; }
  bool IsNotFound() const { return code_ == Code::NotFound; }
  bool IsCorruption() const { return code_ == Code::Corruption; }
  bool IsIOFault() const { return code_ == Code::IOFault; }
  bool IsNotSupported() const { return code_ == Code::NotSupported; }
  bool IsInvalidArgument() const { return code_ == Code::InvalidArgument; }

  std::string ToString() const;

 private:
  Code code_{0};
  code_t msg_key_{0};

  static inline const std::array<std::string_view,
                                 static_cast<size_t>(Error::Code::Count_)>
      code_strings_ = {"Ok", "Not found", "Corruption", "Invalid argument",
                       "IO fault"};
  static_assert(static_cast<size_t>(Error::Code::Count_) ==
                    code_strings_.size(),
                "Update code_strings_ array to reflect Code enum");

  static inline std::atomic<code_t> next_key_{1};
  static inline std::mutex msg_mu_;
  static inline std::map<code_t, std::string> messages_;

  code_t GetNextKey() const {
    code_t key = next_key_.fetch_add(1);
    if (key == 0) {
      key = next_key_.fetch_add(1);
    }
    assert(!messages_.contains(key));
    return key;
  }

  bool HasMessage() const { return msg_key_ != 0; }

  std::string_view GetCodeString() const {
    return (code_ < Code::Count_) ? code_strings_[static_cast<size_t>(code_)]
                                  : "Unknown error code";
  }

  std::string_view GetMessage() const { return messages_.at(msg_key_); }

  void SetMessage(std::string&& msg) const {
    std::lock_guard<std::mutex> guard(msg_mu_);
    messages_[msg_key_] = std::move(msg);
  }

  void DeleteMessage() const {
    std::lock_guard<std::mutex> guard(msg_mu_);
    messages_.erase(msg_key_);
  }
};

}  // namespace leveldb
