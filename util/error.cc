
#include "leveldb/error.h"

namespace leveldb {

Error::~Error() {
  if (HasMessage()) {
    DeleteMessage();
  }
}

Error::Error(const Error& other) { *this = other; }
Error& Error::operator=(const Error& other) {
  if (this != &other) {
    bool has_msg = HasMessage();
    bool other_has_msg = other.HasMessage();
    if (!has_msg && !other_has_msg) {
      // Fast path
    } else if (has_msg && other_has_msg) {
      DeleteMessage();
      SetMessage(std::string{other.GetMessage()});
    } else if (has_msg && !other_has_msg) {
      DeleteMessage();
      msg_key_ = 0;
    } else if (!has_msg && other_has_msg) {
      msg_key_ = GetNextKey();
      SetMessage(std::string{other.GetMessage()});
    }
    code_ = other.code_;
  }
  return *this;
}

Error::Error(Error&& other) noexcept { *this = std::move(other); }
Error& Error::operator=(Error&& other) noexcept {
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

std::string Error::ToString() const {
  std::string error_string{GetCodeString()};
  if (HasMessage()) {
    error_string.append(": ");
    error_string.append(GetMessage());
  }
  return error_string;
}

}  // namespace leveldb
