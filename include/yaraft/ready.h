// Copyright 2017 Wu Tao
// Copyright 2015 The etcd Authors
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

#include <yaraft/memory_storage.h>
#include <yaraft/read_only.h>

namespace yaraft {

// StateType represents the role of a node in a cluster.
enum class StateType { kFollower,
                       kCandidate,
                       kPreCandidate,
                       kLeader,
                       kStateNum };

inline std::ostream& operator<<(std::ostream& os, StateType st) {
  const char* stmap[] = {
      "StateFollower",
      "StateCandidate",
      "StatePreCandidate",
      "StateLeader",
  };
  os << stmap[int(st)];
  return os;
}

// SoftState provides state that is useful for logging and debugging.
// The state is volatile and does not need to be persisted to the WAL.
class SoftState {
 public:
  uint64_t lead;  // must use atomic operations to access; keep 64-bit aligned.
  StateType raftState;

  SoftState& Lead(uint64_t id) {
    lead = id;
    return *this;
  }
  SoftState& RaftState(StateType state) {
    raftState = state;
    return *this;
  }
  friend bool operator==(const SoftState& a, const SoftState& b) {
    return a.lead == b.lead && a.raftState == b.raftState;
  }
  friend bool operator!=(const SoftState& a, const SoftState& b) {
    return !(a == b);
  }
};

enum SnapshotStatus { kSnapshotFinish = 1,
                      kSnapshotFailure = 2 };

// ErrProposalDropped is returned when the proposal is ignored by some cases,
// so that the proposer can be notified and fail fast.
const error_s ErrProposalDropped = error_s::make(ERR_RAFT_RAWNODE, "raft proposal dropped");

// Ready encapsulates the entries and messages that are ready to read,
// be saved to stable storage, committed or sent to other peers.
// All fields in Ready are read-only.
class Ready {
 public:
  // The current volatile state of a Node.
  // SoftState will be nil if there is no update.
  // It is not required to consume or store SoftState.
  std::unique_ptr<SoftState> softState;

  // The current state of a Node to be saved to stable storage BEFORE
  // Messages are sent.
  // hardState will be equal to empty state if there is no update.
  idl::HardState hardState;

  // ReadStates can be used for node to serve linearizable read requests locally
  // when its applied index is greater than the index in ReadState.
  // Note that the readState will be returned when raft receives msgReadIndex.
  // The returned is only valid for the request that requested to read.
  std::vector<ReadState> readStates;

  // `entries` specifies entries to be saved to stable storage BEFORE
  // Messages are sent.
  idl::EntryVec entries;

  // Snapshot specifies the snapshot to be saved to stable storage.
  idl::Snapshot snapshot;

  // CommittedEntries specifies entries to be committed to a
  // store/state-machine. These have previously been committed to stable
  // store.
  idl::EntryVec committedEntries;

  // Messages specifies outbound messages to be sent AFTER Entries are
  // committed to stable storage.
  // If it contains a MsgSnap message, the application MUST report back to raft
  // when the snapshot has been received or has failed by calling ReportSnapshot.
  std::vector<idl::Message> messages;

  // MustSync indicates whether the HardState and Entries must be synchronously
  // written to disk or if an asynchronous write is permissible.
  bool mustSync;

  bool containsUpdates() const {
    return softState != nullptr || !IsEmptyHardState(hardState) ||
           !IsEmptySnap(snapshot) || !entries.empty() ||
           !committedEntries.empty() || !messages.empty() || !readStates.empty();
  }
};

typedef std::unique_ptr<Ready> ReadyUPtr;

}  // namespace yaraft
