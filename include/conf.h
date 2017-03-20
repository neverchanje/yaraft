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

#include "status.h"

namespace yaraft {

class Storage;
// Config contains the parameters to start a raft.
class Config {
 public:
  // id is the identity of the local raft. id cannot be 0.
  uint64_t id;

  // preVote enables the Pre-Vote algorithm described in raft thesis section
  // 9.6. This prevents disruption when a node that has been partitioned away
  // rejoins the cluster.
  bool preVote;

  // electionTick is the number of Node.Tick invocations that must pass between
  // elections. That is, if a follower does not receive any message from the
  // leader of current term before electionTick has elapsed, it will become
  // candidate and start an election. electionTick must be greater than
  // HeartbeatTick. We suggest electionTick = 10 * HeartbeatTick to avoid
  // unnecessary leader switching.
  int electionTick;
  // HeartbeatTick is the number of Node.Tick invocations that must pass between
  // heartbeats. That is, a leader sends heartbeat messages to maintain its
  // leadership every HeartbeatTick ticks.
  int heartbeatTick;

  // Storage is the storage for raft. raft generates entries and states to be
  // stored in storage. raft reads the persisted entries and states out of
  // Storage when it needs. raft reads out the previous state and configuration
  // out of storage when restarting.
  Storage* storage;

  // maxSizePerMsg limits the max size of each append message. Smaller value
  // lowers the raft recovery cost(initial probing and message lost during normal
  // operation). On the other side, it might affect the throughput during normal
  // replication. Note: math.MaxUint64 for unlimited, 0 for at most one entry per
  // message.
  uint64_t maxSizePerMsg;

  // peers contains the IDs of all nodes (including self) in the raft cluster. It
  // should only be set when starting a new raft cluster. Restarting raft from
  // previous configuration will panic if peers is set. peer is private and only
  // used for testing right now.
  std::vector<uint64_t> peers;

  Status Validate() const {
    if (id == 0) {
      return Status::Make(Error::InvalidConfig, "ID cannot be 0");
    }

    if (heartbeatTick <= 0) {
      return Status::Make(Error::InvalidConfig, "heartbeat tick must be greater than 0");
    }

    if (electionTick < heartbeatTick) {
      return Status::Make(Error::InvalidConfig,
                          "election tick must be greater than heartbeat tick");
    }

    if (!storage) {
      return Status::Make(Error::InvalidConfig, "storage cannot be null");
    }

    return Status::OK();
  }
};

}  // namespace yaraft
