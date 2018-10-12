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

#include <yaraft/logger.h>

namespace yaraft {

class Inflights;

// Progress represents a follower’s progress in the view of the leader. Leader maintains
// progresses of all followers, and sends entries to the follower based on its progress.
class Progress {
 public:
  enum StateType { kStateProbe = 0,
                   kStateReplicate,
                   kStateSnapshot };

  ~Progress();

  Progress();

  Progress(Progress &&) = default;
  Progress &operator=(Progress &&) = default;

  // == getter == //

  uint64_t Next() const {
    return next_;
  }
  uint64_t Match() const {
    return match_;
  }
  StateType State() {
    return state_;
  }
  bool Paused() {
    return paused_;
  }
  uint64_t PendingSnapshot() const {
    return pendingSnapshot_;
  }
  bool RecentActive() {
    return recentActive_;
  }
  bool IsLearner() {
    return isLearner_;
  }

 private:
  void resetState(StateType state);

  void becomeProbe() {
    // If the original state is ProgressStateSnapshot, progress knows that
    // the pending snapshot has been sent to this peer successfully, then
    // probes from pendingSnapshot + 1.
    if (state_ == kStateSnapshot) {
      uint64_t pendingSnapshot = pendingSnapshot_;
      resetState(kStateProbe);
      next_ = std::max(match_ + 1, pendingSnapshot + 1);
    } else {
      resetState(kStateProbe);
      next_ = match_ + 1;
    }
  }

  void becomeReplicate() {
    resetState(kStateReplicate);
    next_ = match_ + 1;
  }

  void becomeSnapshot(uint64_t snapshoti) {
    resetState(kStateSnapshot);
    pendingSnapshot_ = snapshoti;
  }

  // maybeUpdate returns false if the given n index comes from an outdated message.
  // Otherwise it updates the progress and returns true.
  bool maybeUpdate(uint64_t n) {
    bool updated = false;
    if (match_ < n) {
      match_ = n;
      updated = true;
      resume();
    }
    if (next_ < n + 1) {
      next_ = n + 1;
    }
    return updated;
  }

  void optimisticUpdate(uint64_t n) { next_ = n + 1; }

  // maybeDecrTo returns false if the given to index comes from an out of order
  // message. Otherwise it decreases the progress next index to min(rejected,
  // last) and returns true.
  bool maybeDecrTo(uint64_t rejected, uint64_t last);

  void pause() { paused_ = true; }
  void resume() { paused_ = false; }

  // IsPaused returns whether sending log entries to this node has been
  // paused. A node may be paused because it has rejected recent
  // MsgApps, is currently waiting for a snapshot, or has reached the
  // MaxInflightMsgs limit.
  bool IsPaused() const;

  void snapshotFailure() { pendingSnapshot_ = 0; }

  // needSnapshotAbort returns true if snapshot progress's Match
  // is equal or higher than the pendingSnapshot.
  bool needSnapshotAbort() const { return state_ == kStateSnapshot && match_ >= pendingSnapshot_; }

  std::string ToString() const;

  friend std::ostream &operator<<(std::ostream &os, const Progress &p) {
    return os << p.ToString();
  }

  // === fluent-style setter === //

  Progress &Ins(size_t cap);
  Progress &PendingSnapshot(uint64_t pending) {
    pendingSnapshot_ = pending;
    return *this;
  }
  Progress &Next(uint64_t next) {
    next_ = next;
    return *this;
  }
  Progress &Match(uint64_t match) {
    match_ = match;
    return *this;
  }
  Progress &State(StateType type) {
    state_ = type;
    return *this;
  }
  Progress &Paused(bool paused) {
    paused_ = paused;
    return *this;
  }
  Progress &IsLearner(bool isLearner) {
    isLearner_ = isLearner;
    return *this;
  }

  // test util
  std::shared_ptr<Progress> Sptr();

  std::shared_ptr<Progress> Clone() const {
    auto p = std::make_shared<Progress>();
    p->next_ = next_;
    p->match_ = match_;
    p->state_ = state_;
    p->paused_ = paused_;
    p->pendingSnapshot_ = pendingSnapshot_;
    p->recentActive_ = recentActive_;
    return p;
  }

 private:
  friend class Raft;
  friend class Network;
  friend class RaftSnapTest;
  friend class RaftTest;
  friend class RaftFlowControlTest;
  friend class ProgressTest;

  uint64_t next_{0}, match_{0};

  // State defines how the leader should interact with the follower.
  //
  // When in ProgressStateProbe, leader sends at most one replication message
  // per heartbeat interval. It also probes actual progress of the follower.
  //
  // When in ProgressStateReplicate, leader optimistically increases next
  // to the latest entry sent after sending replication message. This is
  // an optimized state for fast replicating log entries to the follower.
  //
  // When in ProgressStateSnapshot, leader should have sent out snapshot
  // before and stops sending any replication message.
  StateType state_{kStateProbe};

  // Paused is used in ProgressStateProbe.g
  // When Paused is true, raft should pause sending replication message to this peer.
  bool paused_{false};
  // PendingSnapshot is used in ProgressStateSnapshot.
  // If there is a pending snapshot, the pendingSnapshot will be set to the
  // index of the snapshot. If pendingSnapshot is set, the replication process of
  // this Progress will be paused. raft will not resend snapshot until the pending one
  // is reported to be failed.
  uint64_t pendingSnapshot_{0};

  // RecentActive is true if the progress is recently active. Receiving any messages
  // from the corresponding follower indicates the progress is active.
  // RecentActive can be reset to false after an election timeout.
  bool recentActive_{false};

  // inflights is a sliding window for the inflight messages.
  // Each inflight message contains one or more log entries.
  // The max number of entries per message is defined in raft config as MaxSizePerMsg.
  // Thus inflight effectively limits both the number of inflight messages
  // and the bandwidth each Progress can use.
  // When inflights is full, no more message should be sent.
  // When a leader sends out a message, the index of the last
  // entry should be added to inflights. The index MUST be added
  // into inflights in order.
  // When a leader receives a reply, the previous inflights should
  // be freed by calling inflights.freeTo with the index of the last
  // received entry.
  std::unique_ptr<Inflights> ins_;

  // IsLearner is true if this progress is tracked for a learner.
  bool isLearner_{false};
};

using ProgressSPtr = std::shared_ptr<Progress>;

}  // namespace yaraft
