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

#include <memory>

#include "pb_utils.h"
#include "raft.h"

namespace yaraft {

class RawNode {
 public:
  explicit RawNode(Config* conf) : raft_(new Raft(conf)) {}

  // Tick advances the internal logical clock by a single tick.
  void Tick() {
    raft_->Tick();
  }

  // Step advances the state machine using the given message.
  Status Step(pb::Message& m) {
    // ignore unexpected local messages receiving over network
    if (IsLocalMsg(m)) {
      return Status::Make(Error::StepLocalMsg, "cannot step raft local message");
    }

    if (!raft_->HasPeer(m.from()) && IsResponseMsg(m)) {
      return Status::Make(Error::StepPeerNotFound,
                          "cannot step a response message from peer not found");
    }

    return raft_->Step(m);
  }

  // Campaign causes this RawNode to transition to candidate state.
  Status Campaign() {
    uint64_t id = raft_->Id(), term = raft_->Term();
    return raft_->Step(PBMessage().From(id).To(id).Type(pb::MsgHup).Term(term).v);
  }

 private:
  std::unique_ptr<Raft> raft_;
};

}  // namespace yaraft
