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

#include <algorithm>
#include <cstdint>
#include <sstream>

#include "inflights.h"

#include <fmt/format.h>
#include <glog/logging.h>

namespace yaraft {

class Progress {
 public:
  Progress() = default;

  enum StateType { StateProbe = 0, StateReplicate, StateSnapshot };

  // The index of the next log entry the leader will send to the follower.
  uint64_t NextIndex() const {
    return next_;
  }

  // Highest index applied to peer.
  uint64_t MatchIndex() const {
    return match_;
  }

  Progress& NextIndex(uint64_t next) {
    next_ = next;
    return *this;
  }

  Progress& MatchIndex(uint64_t match) {
    match_ = match;
    return *this;
  }

  Progress& State(StateType type) {
    state_ = type;
    return *this;
  }

  // RecentActive is true if the progress is recently active. Receiving any messages
  // from the corresponding follower indicates the progress is active.
  // RecentActive can be reset to false after an election timeout.
  bool RecentActive() const {
    return recentActive_;
  }

  // PendingSnapshot is used in ProgressStateSnapshot.
  // If there is a pending snapshot, the pendingSnapshot will be set to the
  // index of the snapshot. If pendingSnapshot is set, the replication process of
  // this Progress will be paused. raft will not resend snapshot until the pending one
  // is reported to be failed.
  uint64_t PendingSnapshot() const {
    return pendingSnapshot_;
  }

  Progress& PendingSnapshot(uint64_t pending) {
    pendingSnapshot_ = pending;
    return *this;
  }

  // IsPaused returns whether sending log entries to this node has been
  // paused. A node may be paused because it has rejected recent
  // MsgApps, is currently waiting for a snapshot, or has reached the
  // MaxInflightMsgs limit.
  bool IsPaused() const {
    switch (state_) {
      case StateProbe:
        return paused_;
      case StateSnapshot:
        return true;
      default:
        LOG(FATAL) << "unexpected state";
        break;
    }
  }

  StateType State() const {
    return state_;
  }

  bool NeedSnapshotAbort() const {
    return state_ == StateSnapshot && match_ >= pendingSnapshot_;
  }

  // MaybeDecrTo returns true if nextIndex is decremented.
  bool MaybeDecrTo(uint64_t rejected, uint64_t lastIndex) {
    if (state_ == StateReplicate) {
      // the rejection must be stale if the progress has matched and "rejected"
      // is smaller than "match".
      if (rejected <= match_) {
        return false;
      }

      // directly decrease next to match + 1
      next_ = match_ + 1;
      return true;
    }

    if (next_ - 1 != rejected) {
      return false;
    }

    next_ = std::min(rejected, lastIndex + 1);
    if (next_ < 1) {
      next_ = 1;
    }
    return true;
  }

  // MaybeUpdate returns false if the given index comes from an outdated message.
  // Otherwise it updates the progress (nextIndex and matchIndex) and returns true.
  // Leader will advance commitIndex when the progress updated.
  bool MaybeUpdate(uint64_t index) {
    bool updated = false;

    if (match_ < index) {
      match_ = index;
      updated = true;
    }

    if (next_ < index + 1) {
      next_ = index + 1;
    }
    return updated;
  }

  std::string ToString() const {
    static const char* stateTypeName[] = {"StateProbe", "StateReplicate", "StateSnapshot"};
    return fmt::sprintf("next = %d, match = %d, state = %s, waiting = %s, pendingSnapshot = %d",
                        next_, match_, stateTypeName[state_], IsPaused(), pendingSnapshot_);
  }

  void BecomeReplicate() {
    state_ = StateReplicate;
    paused_ = false;
    pendingSnapshot_ = 0;
    next_ = match_ + 1;
  }

  void BecomeSnapshot(uint64_t snapLastIndex) {
    state_ = StateSnapshot;
    paused_ = false;
    pendingSnapshot_ = snapLastIndex;
  }

  void BecomeProbe() {
    // If the original state is ProgressStateSnapshot, progress knows that
    // the pending snapshot has been sent to this peer successfully, then
    // probes from pendingSnapshot + 1.
    if (state_ == StateSnapshot) {
      next_ = std::max(pendingSnapshot_ + 1, match_ + 1);
    } else {
      next_ = match_ + 1;
    }

    paused_ = false;
    pendingSnapshot_ = 0;
    state_ = StateProbe;
  }

  void Pause() {
    paused_ = true;
  }

  void Resume() {
    paused_ = false;
  }

  // cancel the snapshot
  void SnapshotFailure() {
    pendingSnapshot_ = 0;
  }

 private:
  uint64_t next_;
  uint64_t match_;
  StateType state_;

  bool recentActive_;

  // Paused is used in ProgressStateProbe.
  // When Paused is true, raft should pause sending replication message to this peer.
  bool paused_;

  uint64_t pendingSnapshot_;

  // inflights is a sliding window for the inflight messages.
  // Each inflight message contains one or more log entries.
  // The max number of entries per message is defined in raft config as maxSizePerMsg.
  // Thus inflight effectively limits both the number of inflight messages
  // and the bandwidth each Progress can use.
  // When inflights is full, no more message should be sent.
  // When a leader sends out a message, the index of the last
  // entry should be added to inflights. The index MUST be added
  // into inflights in order.
  // When a leader receives a reply, the previous inflights should
  // be freed by calling inflights.freeTo with the index of the last
  // received entry.
  Inflights ins_;
};

inline std::ostream& operator<<(std::ostream& os, const Progress& p) {
  os << p.ToString();
  return os;
}

}  // namespace yaraft
