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

#include "read_only.h"
#include "slice.h"
#include "status.h"

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

class Raft;
class Config;
class Ready;

struct RaftProgress {
  RaftProgress(uint64_t next, uint64_t match) : nextIndex(next), matchIndex(match) {}
  RaftProgress() : nextIndex(0), matchIndex(0) {}

  uint64_t nextIndex;
  uint64_t matchIndex;
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
  Status Propose(const Slice &data);

  // GetReady returns the current point-in-time state of this RawNode,
  // and returns null when there's no state ready (to be persisted or transferred).
  Ready *GetReady();

  enum SnapshotStatus { kSnapshotFinish = 1, kSnapshotFailure = 2 };

  // ReportSnapshot reports the status of the sent snapshot.
  void ReportSnapshot(uint64_t id, SnapshotStatus status);

  // ReportUnreachable reports the given node is not reachable for the last send.
  void ReportUnreachable(uint64_t id);

  // ProposeConfChange proposes a config change.
  Status ProposeConfChange(const pb::ConfChange &cc);

  // ApplyConfChange applies a config change to the local node.
  pb::ConfState ApplyConfChange(const pb::ConfChange &cc);

  // ReadIndex requests a read state. The read state will be set in ready.
  // Read State has a read index. Once the application advances further than the read
  // index, any linearizable read requests issued before the read request can be
  // processed safely. The read state will have the same rctx attached.
  // TODO(wutao1): using uint64 instead of std::string to serialize read index.
  Status ReadIndex(std::string &ctx);

  // feel free to call this function without lock protection, since the Id
  // never changes.
  uint64_t Id() const;

  uint64_t CurrentTerm() const;

  uint64_t CommittedIndex() const;

  uint64_t LastIndex() const;

  uint64_t LeaderHint() const;

  bool IsLeader() const {
    return LeaderHint() == Id();
  }

  std::unordered_map<uint64_t, RaftProgress> ProgressMap();

 private:
  std::unique_ptr<Raft> raft_;

  std::unique_ptr<pb::HardState> prevHardState_;
};

}  // namespace yaraft