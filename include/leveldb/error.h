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
static consteval auto CodeBaseType_() {
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
  using code_t = decltype(CodeBaseType_());

 public:
  enum class Code : code_t {
    Ok = 0,  // TODO: deprecate after everything is expected returns semantics
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5,
    _Count
  };

  // TODO: deprecate after everything is expected returns semantics
  Error() noexcept {}

  Error(Code c) noexcept : code_(c) {}

  template <typename... Args>
    requires(std::convertible_to<Args, std::string_view> && ...)
  Error(Code c, Args... args) : code_(c), msg_key_(GetNextKey()) {
    std::string_view msgs_arr[] = {std::string_view(args)...};
    auto joined_view = msgs_arr | std::views::join_with(std::string_view(": "));
    std::string buffer;
    std::ranges::copy(joined_view, std::back_inserter(buffer));
    SetMessage(std::move(buffer));
  }

  ~Error() {
    if (HasMessage()) {
      DeleteMessage();
    }
  }

  Error(const Error& other) { *this = other; }
  Error& operator=(const Error& other) {
    if (this != &other) {
      bool has_msg = HasMessage();
      bool other_has_msg = other.HasMessage();
      if (!has_msg && !other_has_msg) {
        // Fast path
      } else if (has_msg && other_has_msg) {
        DeleteMessage();
        std::string msg{other.GetMessage()};
        SetMessage(std::move(msg));
      } else if (has_msg && !other_has_msg) {
        DeleteMessage();
        msg_key_ = 0;
      } else if (!has_msg && other_has_msg) {
        msg_key_ = GetNextKey();
        std::string msg{other.GetMessage()};
        SetMessage(std::move(msg));
      }
      code_ = other.code_;
    }
    return *this;
  }

  Error(Error&& other) noexcept { *this = std::move(other); }
  Error& operator=(Error&& other) noexcept {
    if (this != &other) {
      if (HasMessage()) {
        DeleteMessage();
      }
      code_ = other.code_;
      msg_key_ = other.msg_key_;
      other.code_ = static_cast<Code>(0);
      other.msg_key_ = 0;
    }
    return *this;
  }

  static Error OK() { return Error(Error::Code::Ok); }

  static Error NotFound(std::string_view msg,
                        std::string_view msg2 = std::string_view()) {
    bool has_msg = msg.size() > 0;
    bool has_msg2 = msg2.size() > 0;
    if (has_msg && has_msg2) {
      return Error(Error::Code::NotFound, msg, msg2);
    } else if (has_msg && !has_msg2) {
      return Error(Error::Code::NotFound, msg);
    } else {
      return Error(Error::Code::NotFound);
    }
  }

  static Error Corruption(std::string_view msg,
                          std::string_view msg2 = std::string_view()) {
    bool has_msg = msg.size() > 0;
    bool has_msg2 = msg2.size() > 0;
    if (has_msg && has_msg2) {
      return Error(Error::Code::Corruption, msg, msg2);
    } else if (has_msg && !has_msg2) {
      return Error(Error::Code::Corruption, msg);
    } else {
      return Error(Error::Code::Corruption);
    }
  }

  static Error NotSupported(std::string_view msg,
                            std::string_view msg2 = std::string_view()) {
    bool has_msg = msg.size() > 0;
    bool has_msg2 = msg2.size() > 0;
    if (has_msg && has_msg2) {
      return Error(Error::Code::NotSupported, msg, msg2);
    } else if (has_msg && !has_msg2) {
      return Error(Error::Code::NotSupported, msg);
    } else {
      return Error(Error::Code::NotSupported);
    }
  }

  static Error InvalidArgument(std::string_view msg,
                               std::string_view msg2 = std::string_view()) {
    bool has_msg = msg.size() > 0;
    bool has_msg2 = msg2.size() > 0;
    if (has_msg && has_msg2) {
      return Error(Error::Code::InvalidArgument, msg, msg2);
    } else if (has_msg && !has_msg2) {
      return Error(Error::Code::InvalidArgument, msg);
    } else {
      return Error(Error::Code::InvalidArgument);
    }
  }

  static Error IOError(std::string_view msg,
                       std::string_view msg2 = std::string_view()) {
    bool has_msg = msg.size() > 0;
    bool has_msg2 = msg2.size() > 0;
    if (has_msg && has_msg2) {
      return Error(Error::Code::IOError, msg, msg2);
    } else if (has_msg && !has_msg2) {
      return Error(Error::Code::IOError, msg);
    } else {
      return Error(Error::Code::IOError);
    }
  }

  bool operator==(const Code rhs) const { return code_ == rhs; }

  bool ok() const { return code_ == Code::Ok; }
  bool IsNotFound() const { return code_ == Code::NotFound; }
  bool IsCorruption() const { return code_ == Code::Corruption; }
  bool IsIOError() const { return code_ == Code::IOError; }
  bool IsNotSupportedError() const { return code_ == Code::NotSupported; }
  bool IsInvalidArgument() const { return code_ == Code::InvalidArgument; }

  std::string ToString() const {
    std::string error_string{GetCodeString()};
    if (HasMessage()) {
      error_string.append(": ");
      error_string.append(GetMessage());
    }
    return error_string;
  }

 private:
  Code code_{0};
  code_t msg_key_{0};

  static inline const std::array<std::string_view,
                                 static_cast<size_t>(Code::_Count)>
      code_strings_ = {"Ok", "Not found", "Corruption", "Invalid argument",
                       "IO error"};

  static inline std::atomic<code_t> next_key_{1};
  static inline std::mutex msg_mu_;
  static inline std::map<code_t, std::string> messages_;

  // Can deliberately overflow
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
    return (code_ < Code::_Count) ? code_strings_[static_cast<size_t>(code_)]
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
