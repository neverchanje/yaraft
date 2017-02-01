// Copyright 2017 Wu Tao
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <boost/optional.hpp>
#include <silly/status.h>

namespace yaraft {

class Error {
 public:
  enum ErrorCodes {

    LogCompacted,
    SnapshotOutOfDate,

    // number of error codes
    ErrorCodesNum
  };

 private:
  friend class silly::Status<Error, Error::ErrorCodes>;

  static inline std::string toString(unsigned int errorCode) {
#ifndef(ERROR_CODE_DESCRIPT)
#define ERROR_CODE_DESCRIPT(err) \
  case (err):                    \
    return #err

    ErrorCodes code = static_cast<ErrorCodes>(errorCode);
    switch (code) {
      ERROR_CODE_DESCRIPT(LogCompacted);
      ERROR_CODE_DESCRIPT(SnapshotOutOfDate);
      default:
        return "Unknown";
    }
#undef ERROR_CODE_DESCRIPT
  }
};

typedef silly::Status<Error, Error::ErrorCodes> Status;

template <typename T>
class StatusWith {
 public:
  // for ok case
  StatusWith(T value) : status_(Status::OK()), value_(value) {}

  // for error case
  StatusWith(Status status) : status_(std::move(status)) {}

  StatusWith(Error::ErrorCodes code, const silly::Slice& reason)
      : StatusWith(Status::Make(code, reason)) {}

  const T& GetValue() const {
    assert(status_.IsOK());
    return *value_;
  }

  T& GetValue() {
    return *value_;
  }

  const Status& GetStatus() const {
    return status_;
  }

  bool OK() const {
    return status_.IsOK();
  }

 private:
  Status status_;
  boost::optional<T> value_;
};

}  // namespace yaraft
