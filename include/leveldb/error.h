
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
  ~Error() = default;
  Error(const Error&) = default;
  Error& operator=(const Error&) = default;
  Error(const Error&& other) { *this = std::move(other); }
  Error& operator=(const Error&& other) {
    code_ = other.code_;
    return *this;
  }

  std::string ToString() const;
  ErrorCode code() const;
  bool operator==(const ErrorCode rhs) const { return code() == rhs; }
  bool operator==(const Error& rhs) const { return code() == rhs.code(); }

 private:
  friend class DetailedError;
  Error() = default;

  ErrorCode code_{IS_DETAILED_ERROR_};
  static const ErrorCode IS_DETAILED_ERROR_{static_cast<const ErrorCode>(0)};
};

template <typename T>
concept is_string_like =
    std::same_as<std::string, T> || std::same_as<std::string_view, T> ||
    std::same_as<const char*, T>;  // Unsure if we should allow const char*

class LEVELDB_EXPORT DetailedError : public Error {
 public:
  // Status has support for 1 or 2 messsages, this can take an arbitrary
  // number
  template <typename... Args>
    requires(is_string_like<Args> && ...)
  DetailedError(ErrorCode c, Args... args) {
    std::string_view messages[] = {args...};
    state_ = std::make_unique<std::string>();
    state_->push_back(static_cast<char>(c));
    for (const auto& m : messages) {
      state_->append(m.data(), m.size());
      state_->append(": ", 2);
    }
    state_->resize(state_->size() - 2);  // Remove last ": "
  }

  DetailedError(DetailedError&& other) : Error(std::move(other)) {
    *this = std::move(other);
  }
  DetailedError& operator=(DetailedError&& other) {
    if (this != &other) {
      std::string* others_message = other.state_.release();
      state_.reset(others_message);
    }
    return *this;
  };

  DetailedError(const DetailedError& other) : Error(other) { *this = other; }
  DetailedError& operator=(const DetailedError& other) {
    state_ = std::make_unique<std::string>(*other.state_);
    return *this;
  }

  ~DetailedError() = default;

  ErrorCode code() const { return static_cast<ErrorCode>((*state_)[0]); }

 private:
  friend class Error;
  std::unique_ptr<std::string> state_{nullptr};

  std::string_view message() const {
    return {&(*state_)[1], state_->size() - 1};
  }
};

inline Error::ErrorCode Error::code() const {
  if (code_ != IS_DETAILED_ERROR_) {
    return code_;
  }
  const DetailedError* detailed = static_cast<const DetailedError*>(this);
  return detailed->code();
}

}  // namespace leveldb
