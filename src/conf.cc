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

#include <yaraft/conf.h>

#include "make_unique.h"
#include "stderr_logger.h"

namespace yaraft {

error_s Config::Validate() {
  if (id == 0) {
    return error_s::make(ERR_INVALID_ARGUMENT, "ID cannot be 0");
  }

  if (heartbeatTick <= 0) {
    return error_s::make(ERR_INVALID_ARGUMENT, "heartbeat tick must be greater than 0");
  }

  if (electionTick < heartbeatTick) {
    return error_s::make(ERR_INVALID_ARGUMENT, "election tick must be greater than heartbeat tick");
  }

  if (storage == nullptr) {
    return error_s::make(ERR_INVALID_ARGUMENT, "storage cannot be null");
  }

  if (maxInflightMsgs <= 0) {
    return error_s::make(ERR_INVALID_ARGUMENT, "max inflight messages must be greater than 0");
  }

  if (logger == nullptr) {
    logger = make_unique<StderrLogger>();
  }

  if (readOnlyOption == kReadOnlyLeaseBased && !checkQuorum) {
    return error_s::make(ERR_INVALID_ARGUMENT, "CheckQuorum must be enabled when ReadOnlyOption is ReadOnlyLeaseBased");
  }

  return error_s::ok();
}

}  // namespace yaraft
