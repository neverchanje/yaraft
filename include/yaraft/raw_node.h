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

#include <unordered_map>

#include <yaraft/errors.h>
#include <yaraft/idl_wrapper.h>
#include <yaraft/read_only.h>
#include <yaraft/ready.h>
#include <yaraft/status.h>
#include <yaraft/string_view.h>

namespace yaraft {

class Raft;
class Config;
class Ready;

// ErrStepLocalMsg is returned when try to step a local raft message
const error_s ErrStepLocalMsg =
    error_s::make(ERR_RAFT_RAWNODE, "raft: cannot step raft local message");

// ErrStepPeerNotFound is returned when try to step a response message
// but there is no peer found in raft.prs for that node.
const error_s ErrStepPeerNotFound =
    error_s::make(ERR_RAFT_RAWNODE, "raft: cannot step as peer not found");

class Peer {
 public:
  uint64_t ID;
  std::string Context;
};

// RawNode is a thread-unsafe Node.
class RawNode {
 public:
  // New returns a new RawNode given configuration and a list of raft peers
  static RawNode *New(Config *conf, std::vector<Peer> peers);

  ~RawNode();

  // Tick advances the internal logical clock by a single tick.
  void Tick();

  // Campaign causes this RawNode to transition to candidate state.
  error_s Campaign();

  // Propose proposes data be appended to the raft log.
  error_s Propose(std::string &&data);

  // ProposeConfChange proposes a config change.
  error_s ProposeConfChange(idl::ConfChange cc);

  // ApplyConfChange applies a config change to the local node.
  idl::ConfState ApplyConfChange(idl::ConfChange cc);

  // Step advances the state machine using the given message.
  error_s Step(idl::Message m);

  // Ready returns the current point-in-time state of this RawNode.
  std::unique_ptr<Ready> GetReady();

  // HasReady called when RawNode user need to check if any Ready pending.
  // Checking logic in this method should be consistent with Ready.containsUpdates().
  bool HasReady();

  // Advance notifies the RawNode that the application has applied and saved progress in the
  // last Ready results.
  void Advance(ReadyUPtr rd);

  // GetStatus returns the current status of the given group.
  Status GetStatus();

  // ReportUnreachable reports the given node is not reachable for the last send.
  void ReportUnreachable(uint64_t id);

  // ReportSnapshot reports the status of the sent snapshot.
  void ReportSnapshot(uint64_t id, SnapshotStatus status);

  // TransferLeader tries to transfer leadership to the given transferee.
  void TransferLeader(uint64_t transferee);

  // ReadIndex requests a read state. The read state will be set in ready.
  // Read State has a read index. Once the application advances further than the read
  // index, any linearizable read requests issued before the read request can be
  // processed safely. The read state will have the same rctx attached.
  void ReadIndex(std::string rctx);

 private:
  friend class RawNodeTest;

  ReadyUPtr newReady();

  void commitReady(ReadyUPtr rd);

 private:
  std::unique_ptr<Raft> raft_;
  SoftState prevSoftSt_;
  idl::HardState prevHardSt_;
};

using RawNodeUPtr = std::unique_ptr<RawNode>;

// MustSync returns true if the hard state and count of Raft entries indicate
// that a synchronous write to persistent storage is required.
inline bool MustSync(const idl::HardState &prevst, const idl::HardState &st, size_t entsnum) {
  // Persistent state on all servers:
  // (Updated on stable storage before responding to RPCs)
  // currentTerm
  // votedFor
  // log entries[]
  return entsnum != 0 || st.vote() != prevst.vote() || st.term() != prevst.term();
}

}  // namespace yaraft
