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

#pragma once

#include <memory>

#include "storage.h"
#include "unstable.h"

namespace yaraft {

class RaftLog {
 public:
  bool IsUpToDate(uint64_t index, uint64_t term);

  uint64_t Committed() const;

 private:
  // storage contains all stable entries since the last snapshot.
  std::unique_ptr<Storage> storage_;

  // unstable contains all unstable entries and snapshot.
  // they will be saved into storage.
  Unstable unstable_;

  /// The following variables are volatile states kept on all servers, as referenced in raft paper
  /// Figure 2.
  /// Invariant: commitIndex >= lastApplied.

  // Index of highest log entry known to be committed (initialized to 0, increases monotonically)
  uint64_t commitIndex_;
  // Index of highest log entry applied to state machine (initialized to 0, increases monotonically)
  uint64_t lastApplied_;
};

}  // namespace yaraft
