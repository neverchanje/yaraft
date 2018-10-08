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

#include <yaraft/progress.h>

#include "inflights.h"
#include "make_unique.h"

namespace yaraft {

Progress::Progress() = default;

Progress::~Progress() = default;

void Progress::resetState(Progress::StateType state) {
  state_ = state;
  paused_ = false;
  pendingSnapshot_ = 0;
  ins_->reset();
}

bool Progress::maybeDecrTo(uint64_t rejected, uint64_t last) {
  if (state_ == kStateReplicate) {
    // the rejection must be stale if the progress has matched and "rejected"
    // is smaller than "match".
    if (rejected <= match_) {
      return false;
    }

    // directly decrease next to match + 1
    next_ = match_ + 1;
    return true;
  }

  // the rejection must be stale if "rejected" does not match next - 1
  if (next_ - 1 != rejected) {
    return false;
  }

  next_ = std::min(rejected, last + 1);
  if (next_ < 1) {
    next_ = 1;
  }
  resume();
  return true;
}

std::string Progress::ToString() const {
  static const char *stateTypeName[] = {"StateProbe", "StateReplicate", "StateSnapshot"};
  return fmt::sprintf("next = %d, match = %d, state = %s, waiting = %s, pendingSnapshot = %d", next_, match_,
                      stateTypeName[state_], IsPaused(), pendingSnapshot_);
}

bool Progress::IsPaused() const {
  switch (state_) {
    case kStateProbe:
      return paused_;
    case kStateReplicate:
      return ins_->full();
    case kStateSnapshot:
      return true;
    default:
      RAFT_PANIC("unexpected state");
      __builtin_unreachable();
  }
}

Progress &Progress::Ins(size_t cap) {
  ins_ = make_unique<Inflights>(cap);
  return *this;
}

std::shared_ptr<Progress> Progress::Sptr() {
  return std::make_shared<Progress>(std::move(*this));
}

}  // namespace yaraft
