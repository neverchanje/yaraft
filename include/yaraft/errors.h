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

#pragma once

#include <yaraft/logger.h>
#include <yaraft/string_view.h>

#include <cassert>
#include <memory>
#include <sstream>
#include <utility>

namespace yaraft {

class error_code {
 public:
  constexpr error_code() = default;

  explicit constexpr error_code(int err) noexcept : value_(err) {}

  constexpr operator int() const noexcept {  // NOLINT
    return value_;
  }

  const char *to_string() const;

 private:
  int value_{0};
};

const error_code ERR_OK(0);
const error_code ERR_INVALID_ARGUMENT(1);
const error_code ERR_RAFT_STORAGE(2);
const error_code ERR_RAFT_RAWNODE(3);

inline const char *error_code::to_string() const {
  switch (value_) {
    case 0:
      return "ERR_OK";
    case 1:
      return "ERR_INVALID_ARGUMENT";
    case 2:
      return "ERR_RAFT_STORAGE";
    case 3:
      return "ERR_RAFT_RAWNODE";
    default:
      assert(false);
  }
}

// error_s gives a detailed description of the error tagged by error_code.
// For example:
//
//   error_s open_file(std::string file_name) {
//       if(file_name.empty()) {
//           return error_s::make(ERR_INVALID_PARAMETERS, "file name should not
//           be empty");
//         }
//       return error_s::ok();
//   }
//
//   error_s err = open_file("");
//   if (!err.is_ok()) {
//       std::cerr << s.description() << std::endl;
//       // print: "ERR_INVALID_PARAMETERS: file name should not be empty"
//   }
//
class error_s {
 public:
  constexpr error_s() noexcept = default;

  ~error_s() = default;

  // copyable and movable
  error_s(const error_s &rhs) noexcept = default;
  error_s &operator=(const error_s &rhs) noexcept = default;
  error_s(error_s &&rhs) noexcept = default;
  error_s &operator=(error_s &&) noexcept = default;

  static error_s make(error_code code, string_view reason) noexcept {
    return error_s(code, reason);
  }

  static error_s make(error_code code) {
    // fast path
    if (code == ERR_OK) {
      return {};
    }
    return make(code, "");
  }

  // Return a success status.
  // This function is almost zero-cost since the returned object contains
  // merely a null pointer.
  static error_s ok() { return error_s(); }

  bool is_ok() const {
    if (_info) {
      return _info->code == ERR_OK;
    }
    return true;
  }

  std::string description() const {
    if (!_info) {
      return ERR_OK.to_string();
    }
    std::string code = _info->code.to_string();
    return _info->msg.empty() ? code : code + ": " + _info->msg;
  }

  error_code code() const { return _info ? error_code(_info->code) : ERR_OK; }

  error_s &operator<<(const char str[]) {
    if (_info) {
      _info->msg.append(str);
    }
    return (*this);
  }

  template <class T>
  error_s &operator<<(T v) {
    if (_info) {
      std::ostringstream oss;
      oss << v;
      (*this) << oss.str();
    }
    return *this;
  }

  friend std::ostream &operator<<(std::ostream &os, const error_s &s) {
    return os << s.description();
  }

  friend bool operator==(const error_s &lhs, const error_s &rhs) {
    // fast path
    if (lhs._info == rhs._info) {
      return true;
    }
    if (lhs.is_ok()) {
      return lhs.is_ok() && rhs.is_ok();
    }

    return lhs.code() == rhs.code() && lhs._info->msg == rhs._info->msg;
  }

 private:
  error_s(error_code code, string_view msg) noexcept
      : _info(new error_info(code, msg)) {}

  struct error_info {
    error_code code;
    std::string msg;  // TODO(wutao1): use raw char* to improve performance?

    error_info(error_code c, string_view s)
        : code(c), msg(s.data(), s.size()) {}
  };

 private:
  std::shared_ptr<error_info> _info;
};

// error_with is used to return an error or a value.
// For example:
//
//   error_with<int> result = ...;
//   if (!s.is_ok()) {
//       cerr << s.get_error().description()) << endl;
//   } else {
//       cerr << s.get_value() << endl;
//   }
//
template <typename T>
class error_with {
 public:
  // for ok case
  error_with(T value)  // NOLINT(explicit)
      : _value(new T(std::move(value))) {}

  // for error case
  error_with(error_s status)  // NOLINT(explicit)
      : _err(std::move(status)) {
    assert(!_err.is_ok());
  }

  const T &get_value() const {
    if (!is_ok()) {
      RAFT_PANIC(get_error());
    }
    return *_value;
  }

  T &get_value() {
    if (!is_ok()) {
      RAFT_PANIC(get_error());
    }
    return *_value;
  }

  const error_s &get_error() const { return _err; }

  error_s &get_error() { return _err; }

  bool is_ok() const { return _err.is_ok(); }

 private:
  error_s _err;
  std::unique_ptr<T> _value;
};

}  // namespace yaraft
