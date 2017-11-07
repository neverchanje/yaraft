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
#include <deque>
#include <unordered_set>

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

// ReadState provides state for read only query.
// It's caller's responsibility to call ReadIndex first before getting
// this state from ready, it's also caller's duty to differentiate if this
// state is what it requests through RequestCtx, eg. given a unique id as
// RequestCtx
struct ReadState {
  uint64_t index;
  std::string requestCtx;
};

struct ReadIndexStatus {
  uint64_t index;

  std::unordered_set<uint64_t> acks;
};

struct ReadOnly {
  // addRequest adds a read only reuqest into readonly struct.
  // `index` is the commit index of the raft state machine when it received
  // the read only request.
  // `m` is the original read only request message from the local or remote node.
  void AddRequest(uint64_t idx, const pb::Message &m);

  // RecvAck notifies the readonly struct that the raft state machine received
  // an acknowledgment of the heartbeat that attached with the read only request
  // context.
  int RecvAck(const pb::Message &m);

  // Advance advances the read only request queue kept by the readonly struct.
  // It dequeues the requests until it finds the read only request that has
  // the same context as the given `m`.
  void Advance(const pb::Message &m, std::vector<ReadState> *readStates);

  std::map<std::string, ReadIndexStatus> pendingReadIndex;
  std::deque<std::string> readIndexQueue;
};

}  // namespace yaraft