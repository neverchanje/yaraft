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

#include <cstdint>
#include <vector>

#include <yaraft/errors.h>
#include <yaraft/logger.h>

namespace yaraft {

class Storage;

enum ReadOnlyOption {
  // ReadOnlySafe guarantees the linearizability of the read only request by
  // communicating with the quorum. It is the default and suggested option.
  kReadOnlySafe,
  // ReadOnlyLeaseBased ensures linearizability of the read only request by
  // relying on the leader lease. It can be affected by clock drift.
  // If the clock drift is unbounded, leader might keep the lease longer than it
  // should (clock can move backward/pause without any bound). ReadIndex is not
  // safe in that case.
  kReadOnlyLeaseBased
};

// Config contains the parameters to start a raft.
class Config {
 public:
  // id is the identity of the local raft. id cannot be 0.
  uint64_t id{0};

  // ElectionTick is the number of Node.Tick invocations that must pass between
  // elections. That is, if a follower does not receive any message from the
  // leader of current term before ElectionTick has elapsed, it will become
  // candidate and start an election. ElectionTick must be greater than
  // HeartbeatTick. We suggest ElectionTick = 10 * HeartbeatTick to avoid
  // unnecessary leader switching.
  int electionTick{0};
  // HeartbeatTick is the number of Node.Tick invocations that must pass between
  // heartbeats. That is, a leader sends heartbeat messages to maintain its
  // leadership every HeartbeatTick ticks.
  int heartbeatTick{0};

  // Storage is the storage for raft. raft generates entries and states to be
  // stored in storage. raft reads the persisted entries and states out of
  // Storage when it needs. raft reads out the previous state and configuration
  // out of storage when restarting.
  Storage* storage{nullptr};
  // Applied is the last applied index. It should only be set when restarting
  // raft. raft will not return entries to the application smaller or equal to
  // Applied. If Applied is unset when restarting, raft might return previous
  // applied entries. This is a very application dependent configuration.
  uint64_t applied{0};

  // MaxSizePerMsg limits the max size of each append message. Smaller value
  // lowers the raft recovery cost(initial probing and message lost during normal
  // operation). On the other side, it might affect the throughput during normal
  // replication. Note: math.MaxUint64 for unlimited, 0 for at most one entry per
  // message.
  uint64_t maxSizePerMsg{0};
  // MaxInflightMsgs limits the max number of in-flight append messages during
  // optimistic replication phase. The application transportation layer usually
  // has its own sending buffer over TCP/UDP. Setting MaxInflightMsgs to avoid
  // overflowing that sending buffer. TODO (xiangli): feedback to application to
  // limit the proposal rate?
  size_t maxInflightMsgs{0};

  // CheckQuorum specifies if the leader should check quorum activity. Leader
  // steps down when quorum is not active for an electionTimeout.
  bool checkQuorum{};

  // PreVote enables the Pre-Vote algorithm described in raft thesis section
  // 9.6. This prevents disruption when a node that has been partitioned away
  // rejoins the cluster.
  bool preVote{false};

  // ReadOnlyOption specifies how the read only request is processed.
  //
  // ReadOnlySafe guarantees the linearizability of the read only request by
  // communicating with the quorum. It is the default and suggested option.
  //
  // ReadOnlyLeaseBased ensures linearizability of the read only request by
  // relying on the leader lease. It can be affected by clock drift.
  // If the clock drift is unbounded, leader might keep the lease longer than it
  // should (clock can move backward/pause without any bound). ReadIndex is not safe
  // in that case.
  // CheckQuorum MUST be enabled if ReadOnlyOption is ReadOnlyLeaseBased.
  ReadOnlyOption readOnlyOption{};

  // Logger is the logger used for raft log. For multinode which can host
  // multiple raft group, each raft group can have its own logger
  std::unique_ptr<Logger> logger;

  // DisableProposalForwarding set to true means that followers will drop
  // proposals, rather than forwarding them to the leader. One use case for
  // this feature would be in a situation where the Raft leader is used to
  // compute the data of a proposal, for example, adding a timestamp from a
  // hybrid logical clock to data in a monotonically increasing way. Forwarding
  // should be disabled to prevent a follower with an inaccurate hybrid
  // logical clock from assigning the timestamp and then forwarding the data
  // to the leader.
  bool disableProposalForwarding{false};

  error_s Validate();

  Config() = default;

 private:
  friend class Raft;
  friend class BaseTest;

  // peers contains the IDs of all nodes (including self) in the raft cluster. It
  // should only be set when starting a new raft cluster. Restarting raft from
  // previous configuration will panic if peers is set. peer is private and only
  // used for testing right now.
  std::vector<uint64_t> peers;

  // learners contains the IDs of all learner nodes (including self if the
  // local node is a learner) in the raft cluster. learners only receives
  // entries from the leader node. It does not vote or promote itself.
  std::vector<uint64_t> learners;
};

}  // namespace yaraft
