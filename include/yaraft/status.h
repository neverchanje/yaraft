// Copyright 2017 The etcd Authors
// Copyright 2018 Wu Tao
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

#include <cstdint>
#include <map>

#include <yaraft/progress.h>
#include <yaraft/ready.h>

namespace yaraft {

class Status {
 public:
  uint64_t id{0};

  idl::HardState hardState;
  SoftState softState;

  uint64_t applied{0};
  std::map<uint64_t, ProgressSPtr> progress;

  uint64_t leadTransferee{0};

  // MarshalJSON translates the raft status into JSON.
  // TODO: try to simplify this by introducing ID type into raft
  std::string MarshalJSON() const;
};

}  // namespace yaraft
