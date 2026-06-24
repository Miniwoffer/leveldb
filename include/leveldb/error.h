
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

namespace {
constexpr const size_t PTR_SZ = sizeof(std::unique_ptr<std::string>);

consteval auto EnumBaseType() {
  if constexpr (PTR_SZ == 8) {
    return uint_least32_t{};
  } else {
    return []() { static_assert(PTR_SZ == 8, "Unsupported pointer size"); }();
  }
}

using enum_base_type = decltype(EnumBaseType());

template <typename T>
concept is_string_like =
    std::same_as<std::string, T> || std::same_as<std::string_view, T> ||
    std::same_as<const char*, T>;  // Unsure if we should allow const char*

}  // namespace

class LEVELDB_EXPORT Error {
 public:
  enum class Code : enum_base_type {
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5,
  };

  template <typename... Args>
    requires(is_string_like<Args> && ...)
  Error(Code c, Args... args) {
    std::string_view messages[] = {args...};
    string_ = std::make_unique<std::string>();
    string_->push_back(static_cast<char>(c));
    for (const auto& m : messages) {
      string_->append(m.data(), m.size());
      string_->append(": ", 2);
    }
    string_->resize(string_->size() - 2);  // Remove last ": "
  }

  Error(Code c) : code_(c) { pad_.back() = 1; }

  ~Error() {
    if (IsString()) {
      string_.reset();
    }
  }

  Code code() const {
    return IsString() ? static_cast<Code>((*string_)[0]) : code_;
  }

  bool operator==(const Code rhs) { return code() == rhs; }
  bool operator==(const Error& rhs) { return code() == rhs.code(); }

  std::string ToString() const {
    std::string error_string;
    switch (code()) {
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

    if (!IsString()) {
      return error_string;
    }

    error_string.append(": ");
    error_string.append(*Message());
    return error_string;
  }

 private:
  union {
    std::unique_ptr<std::string> string_{nullptr};
    struct {
      Code code_;
      std::array<char, PTR_SZ - sizeof(code_)> pad_;
    };
  };

  std::optional<std::string> Message() const {
    if (IsString()) {
      return string_->substr(1, string_->size() - 1);
    }
    return std::nullopt;
  }

  // Relies on user space addresses always being in [0, 2^48), which means bytes
  // 7 and 8 of a user space pointer will always be 0. This check will, in this
  // case, always return true when the Error was initialized as a string
  // pointer, and false when it was initialized as an enum, where the
  // constructor sets the value of pad_.back() to 1.
  // While this assumption is very likely to hold for most 64-bit OSs, it might
  // not be true on 32-bit, so the design is not especially portable, and more
  // of an exercise in over-engineering for fun :)
  bool IsString() const { return pad_.back() == 0; }
};

}  // namespace leveldb
