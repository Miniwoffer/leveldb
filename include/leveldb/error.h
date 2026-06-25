
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

namespace {
consteval auto CodeBaseType() {
  constexpr const size_t ptr_sz = sizeof(size_t);
  if constexpr (ptr_sz == 8) {
    return uint32_t{};
  } else if constexpr (ptr_sz == 4) {
    return uint16_t{};
  } else {
    return unsigned{};
  }
}

using code_t = decltype(CodeBaseType());

template <typename T>
concept is_string_like =
    std::same_as<std::string, T> || std::same_as<std::string_view, T> ||
    std::same_as<const char*, T> || std::same_as<const char&, T>;
}  // namespace

class LEVELDB_EXPORT Error {
 public:
  enum class Code : code_t {
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5,
  };

  Error(Code c) noexcept : code_(c) {}

  template <typename... Args>
    requires(is_string_like<Args> && ...)
  Error(Code c, Args... args) : code_(c), msg_key_(GetNextKey()) {
    std::string_view messages[] = {args...};
    std::string message;
    for (const auto& m : messages) {
      message.append(m);
      message.append(": ");
    }
    message.resize(message.size() - 2);  // Remove last ": "
    SetMessage(std::move(message));
  }

  ~Error() {
    if (HasMessage()) {
      EraseMessage();
    }
  }

  Error(const Error& other) { *this = other; }
  Error& operator=(const Error& other) {
    if (HasMessage()) {
      EraseMessage();
    }
    code_ = other.code_;
    // std::map::erase does nothing if no element with the provided key
    // exists, so multiple Errors calling erase on the same key is safe,
    // and this prevents creating copies of the same error message.
    msg_key_ = other.msg_key_;
    return *this;
  }

  Error(Error&& other) noexcept { *this = std::move(other); }
  Error& operator=(Error&& other) noexcept {
    if (this != &other) {
      code_ = other.code_;
      msg_key_ = other.msg_key_;
      other.code_ = static_cast<Code>(0);
      other.msg_key_ = 0;
    }
    return *this;
  }

  bool operator==(const Code rhs) const { return code_ == rhs; }

  std::string ToString() const {
    std::string error_string;
    switch (code_) {
      case Code::NotFound:
        error_string = "Not found";
        break;
      case Code::Corruption:
        error_string = "Corruption";
        break;
      case Code::NotSupported:
        error_string = "Not implemented";
        break;
      case Code::InvalidArgument:
        error_string = "Invalid argument";
        break;
      case Code::IOError:
        error_string = "IO error";
        break;
      default:
        return "Invalid error code";
    }

    if (HasMessage()) {
      error_string.append(": ");
      error_string.append(GetMessage());
    }

    return error_string;
  }

 private:
  struct {
    Code code_{0};
    code_t msg_key_{0};
  };

  inline static std::atomic<code_t> next_key_{1};
  inline static std::mutex msg_mu_;
  inline static std::map<code_t, std::string> messages_;

  // Can deliberately overflow
  code_t GetNextKey() {
    code_t z = 0, o = 1;
    next_key_.compare_exchange_strong(z, o);
    code_t key = next_key_.fetch_add(1);
    assert(!messages_.contains(key));
    return key;
  }

  bool HasMessage() const { return msg_key_ != 0; }

  const std::string& GetMessage() const {
    // const version of .at() is thread-safe
    return messages_.at(msg_key_);
  }

  void SetMessage(const std::string& msg) const {
    std::lock_guard<std::mutex> guard(msg_mu_);
    messages_[msg_key_] = msg;
  }

  void SetMessage(const std::string&& msg) const {
    std::lock_guard<std::mutex> guard(msg_mu_);
    messages_[msg_key_] = std::move(msg);
  }

  void EraseMessage() const {
    std::lock_guard<std::mutex> guard(msg_mu_);
    messages_.erase(msg_key_);
  }
};

}  // namespace leveldb
