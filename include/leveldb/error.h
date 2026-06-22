
#include <cassert>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "leveldb/export.h"

namespace leveldb {

class LEVELDB_EXPORT Error {
  friend class DetailedError;

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
  ErrorCode code_;
};

class LEVELDB_EXPORT DetailedError : public Error {
 public:
  template <typename... Args>
    requires(std::same_as<std::string_view, Args> && ...)
  DetailedError(ErrorCode c, Args... args) : Error(c) {
    std::string_view messages[] = {args...};
    message_ = std::make_unique<std::string>();
    for (auto m : messages) {
      *message_ += std::string(m) + std::string(": ");
    }
    // Remove last ": "
    message_->resize(message_->size() - 2);
  }

  DetailedError& operator=(DetailedError&& other) {
    if (this != &other) {
      message_.reset(other.message_.release());
      code_ = other.code();
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
      case Corruption:
        error_string = "Corruption: ";
      case NotSupported:
        error_string = "Not implemented: ";
      case InvalidArgument:
        error_string = "Invalid argument: ";
      case IOError:
        error_string = "IO error: ";
      default:
        assert(0);
        return "Invalid error code";
    }
    error_string += *message_;
    return error_string;
  }

 private:
  std::unique_ptr<std::string> message_{nullptr};
};

}  // namespace leveldb
