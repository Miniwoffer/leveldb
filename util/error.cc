
#include "leveldb/error.h"

namespace leveldb {

std::string Error::ToString() const {
  if (code_ != IS_DETAILED_ERROR_) {
    switch (code()) {
      case Error::NotFound:
        return "Not found";
      case Error::Corruption:
        return "Corruption";
      case Error::NotSupported:
        return "Not implemented";
      case Error::InvalidArgument:
        return "Invalid argument";
      case Error::IOError:
        return "IO error";
      default:
        assert(0);
        return "Invalid error code";
    }
  }

  const DetailedError* detailed = static_cast<const DetailedError*>(this);
  std::string error_string;
  switch (detailed->code()) {
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

  error_string.append(detailed->message());
  return error_string;
}

}  // namespace leveldb
