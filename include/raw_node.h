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

#include "status.h"

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

class Raft;
class Config;
class Ready;

struct RaftInfo {
  uint64_t currentLeader;
  uint64_t currentTerm;
  uint64_t logIndex;
};

class RawNode {
 public:
  explicit RawNode(Config *conf);

  ~RawNode();

  // Tick advances the internal logical clock by a single tick.
  void Tick();

  // Step advances the state machine using the given message.
  Status Step(pb::Message &m);

  // Campaign causes this RawNode to transition to candidate state.
  Status Campaign();

  // Propose proposes data be appended to the raft log.
  Status Propose(const silly::Slice& data);

  // GetReady returns the current point-in-time state of this RawNode,
  // and returns null when there's no state ready (to be persisted or transferred).
  Ready *GetReady();

  // Advance notifies the RawNode that the application has applied and saved progress in the
  // last Ready results.
  void Advance(const Ready &ready);

  const Config *GetConfig() const;

  RaftInfo GetInfo() const;

 private:
  // auto-deleted
  Raft *raft_;

  std::unique_ptr<pb::HardState> prevHardState_;
};

}  // namespace yaraft