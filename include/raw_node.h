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

#include "raftpb.pb.h"
#include "status.h"

namespace yaraft {

class Raft;
class Config;

class RawNode {
 public:
  explicit RawNode(Config *conf);

  // Tick advances the internal logical clock by a single tick.
  void Tick();

  // Step advances the state machine using the given message.
  Status Step(pb::Message &m);

  // Campaign causes this RawNode to transition to candidate state.
  Status Campaign();

 private:
  std::unique_ptr<Raft> raft_;
};

}  // namespace yaraft