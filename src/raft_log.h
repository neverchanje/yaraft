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

#include <fmt/format.h>
#include <glog/logging.h>

namespace yaraft {

class RaftLog {
  /// |<- firstIndex          lastIndex->|
  /// |-- Storage---|----- Unstable -----|
  /// |=============|====================|

 public:
  RaftLog(Storage *storage) : storage_(storage) {
    auto s = storage_->FirstIndex();
    if (!s.GetStatus().OK()) {
      LOG(FATAL) << s.GetStatus();
    }
  }

  // Raft determines which of two logs is more up-to-date
  // by comparing the index and term of the last entries in the
  // logs. If the logs have last entries with different terms, then
  // the log with the later term is more up-to-date. If the logs
  // end with the same term, then whichever log is longer is
  // more up-to-date. (Raft paper 5.4.1)
  bool IsUpToDate(uint64_t index, uint64_t term) {
    return (term > LastTerm()) || (term == LastTerm() && index >= LastIndex());
  }

  uint64_t Committed() const {}

  StatusWith<uint64_t> Term(uint64_t index) const {
    // the valid term range is [index of dummy entry, last index]
    auto dummyIndex = FirstIndex() - 1;
    if (index > LastIndex() || index < dummyIndex) {
      return Status::Make(Error::OutOfBound);
    }

    auto pTerm = unstable_.MaybeTerm(index);
    if (!pTerm) {
      return pTerm.get();
    }

    auto s = storage_->Term(index);
    if (s.OK()) {
      return s.GetValue();
    }

    auto errorCode = s.GetStatus().Code();
    if (errorCode == Error::OutOfBound) {
      return s.GetStatus();
    }

    // unacceptable error
    LOG(FATAL) << s.GetStatus();
    return 0;
  }

  uint64_t LastIndex() const {
    if (!unstable_.Empty()) {
      return unstable_.entries.rbegin()->index();
    } else {
      auto s = storage_->LastIndex();
      if (!s.OK()) {
        LOG(FATAL) << s.GetStatus();
      }
      return s.GetValue();
    }
  }

  uint64_t LastTerm() const {
    auto s = Term(LastIndex());
    if (!s.OK()) {
      LOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  uint64_t FirstIndex() const {
    auto s = storage_->FirstIndex();
    if (!s.OK()) {
      LOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

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
