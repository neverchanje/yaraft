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

#include "conf.h"
#include "raft.h"
#include "raftpb.pb.h"
#include "storage.h"

namespace yaraft {

class Network {
 public:
  Network(Config *cfg) {}

  void Send(const pb::Message &msg) {
    msg_.push_back(msg);
  }

 private:
  std::vector<pb::Message> msg_;
};

Config *newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                      Storage *storage) {
  auto conf = new Config();
  conf->id = id;
  conf->electionTick = election;
  conf->heartbeatTick = heartbeat;
  conf->storage = storage;
  conf->peers = std::move(peers);
  return conf;
}

Raft *newTestRaft(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                  Storage *storage) {
  return new Raft(newTestConfig(id, peers, election, heartbeat, storage));
}

}  // namespace yaraft
