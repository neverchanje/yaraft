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

#include <stdexcept>

#include <fmt/format.h>

namespace yaraft {

class RaftError : public std::exception {
 public:
  template <class... Args>
  explicit RaftError(const fmt::CStringRef fmtString, Args... args) {
    msg_ = fmt::format(fmtString, std::forward<Args>(args)...);
  }

  virtual ~RaftError() = default;

  virtual const char* what() const noexcept {
    return msg_.c_str();
  }

 protected:
  std::string msg_;
};

}  // namespace yaraft