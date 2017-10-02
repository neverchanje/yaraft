// Copyright 2017 The etcd Authors
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

#include "conf.h"

namespace yaraft {

Status Config::Validate() {
  if (id == 0) {
    return Status::Make(Error::InvalidConfig, "ID cannot be 0");
  }

  if (heartbeatTick <= 0) {
    return Status::Make(Error::InvalidConfig, "heartbeat tick must be greater than 0");
  }

  if (electionTick < heartbeatTick) {
    return Status::Make(Error::InvalidConfig, "election tick must be greater than heartbeat tick");
  }

  if (!storage) {
    return Status::Make(Error::InvalidConfig, "storage cannot be null");
  }

  return Status::OK();
}

Config::Config() : id(0), heartbeatTick(0), electionTick(0), storage(nullptr) {}

}  // namespace yaraft