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

#include "status.h"
#include "logging.h"

namespace yaraft {

#define DUMB_ERROR_TO_STRING(err) \
  case (err):                     \
    return #err

std::string Error::toString(unsigned int errorCode) {
  ErrorCodes code = static_cast<ErrorCodes>(errorCode);
  switch (code) {
    DUMB_ERROR_TO_STRING(OK);
    DUMB_ERROR_TO_STRING(OutOfBound);
    DUMB_ERROR_TO_STRING(InvalidConfig);
    DUMB_ERROR_TO_STRING(LogCompacted);
    DUMB_ERROR_TO_STRING(StepLocalMsg);
    DUMB_ERROR_TO_STRING(StepPeerNotFound);
    default:
      FMT_LOG(FATAL, "Unknown error code: {}", code);
      return "";
  }
}

}  // namespace yaraft
