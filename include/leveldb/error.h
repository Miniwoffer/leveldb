
#include <cassert>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "leveldb/export.h"

namespace leveldb {

class LEVELDB_EXPORT Error {
 public:
  enum ErrorCode {
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5,
  };

  Error(ErrorCode c) noexcept : code_(c) {}
  Error& operator=(Error&& other) {
    code_ = other.code_;
    return *this;
  }
  Error(Error&& other) { *this = std::move(other); }

  Error& operator=(const Error&) = default;
  Error(const Error&) = default;
  ~Error() = default;

  ErrorCode code() const { return code_; }

  virtual std::string ToString() const {
    switch (code()) {
      case NotFound:
        return "Not found";
      case Corruption:
        return "Corruption";
      case NotSupported:
        return "Not implemented";
      case InvalidArgument:
        return "Invalid argument";
      case IOError:
        return "IO error";
      default:
        assert(0);
        return "Invalid error code";
    }
  }

  bool operator==(const ErrorCode other) const { return code() == other; }
  bool operator==(const Error& other) const { return code() == other.code(); }

 private:
  friend class DetailedError;
  ErrorCode code_;
};

template <typename T>
concept is_string_like =
    std::same_as<std::string, T> || std::same_as<std::string_view, T> ||
    std::same_as<const char*, T>;  // Unsure if we should allow const char*

class LEVELDB_EXPORT DetailedError : public Error {
 public:
  template <typename... Args>
    requires(is_string_like<Args> && ...)
  DetailedError(ErrorCode c, Args... args) : Error(c) {
    std::string messages[] = {args...};
    message_ = std::make_unique<std::string>();
    for (auto m : messages) {
      *message_ += m + std::string(": ");
    }
    // Remove last ": "
    message_->resize(message_->size() - 2);
  }

  DetailedError& operator=(DetailedError&& other) {
    if (this != &other) {
      std::string* others_message = other.message_.release();
      message_.reset(others_message);
      code_ = other.code_;
      other.code_ = static_cast<ErrorCode>(0);
    }
    return *this;
  };

  DetailedError(DetailedError&& other) : Error(std::move(other)) {
    *this = std::move(other);
  }

  DetailedError(const DetailedError&) = default;
  DetailedError& operator=(const DetailedError&) = default;
  ~DetailedError() = default;

  std::string ToString() const {
    std::string error_string;
    switch (code()) {
      case NotFound:
        error_string = "Not found: ";
        break;
      case Corruption:
        error_string = "Corruption: ";
        break;
      case NotSupported:
        error_string = "Not implemented: ";
        break;
      case InvalidArgument:
        error_string = "Invalid argument: ";
        break;
      case IOError:
        error_string = "IO error: ";
        break;
      default:
        return "Invalid error code";
    }
    error_string += *message_;
    return error_string;
  }

 private:
  std::unique_ptr<std::string> message_{nullptr};
};

}  // namespace leveldb
