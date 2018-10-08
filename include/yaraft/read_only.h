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

#include <yaraft/conf.h>
#include <yaraft/idl_wrapper.h>

#include <unordered_set>

namespace yaraft {

// ReadState provides state for read only query.
// It's caller's responsibility to call ReadIndex first before getting
// this state from ready, it's also caller's duty to differentiate if this
// state is what it requests through RequestCtx, eg. given a unique id as
// RequestCtx
class ReadState {
 public:
  uint64_t index;
  std::string requestCtx;

  ReadState &Index(uint64_t i) {
    index = i;
    return *this;
  }
  ReadState &RequestCtx(std::string r) {
    requestCtx = std::move(r);
    return *this;
  }

  friend bool operator==(const ReadState &r1, const ReadState &r2) {
    return r1.index == r2.index && r1.requestCtx == r2.requestCtx;
  }
};

struct ReadIndexStatus {
  idl::Message req;
  uint64_t index{0};
  std::unordered_set<uint64_t> acks;
};

class ReadOnly {
 public:
  ReadOnlyOption option;
  std::map<std::string, ReadIndexStatus> pendingReadIndex;
  GoSlice<std::string> readIndexQueue;

  explicit ReadOnly(ReadOnlyOption opt) : option(opt) {}

  // addRequest adds a read only reuqest into readonly struct.
  // `index` is the commit index of the raft state machine when it received
  // the read only request.
  // `m` is the original read only request message from the local or remote node.
  void addRequest(uint64_t idx, idl::Message m);

  // recvAck notifies the readonly struct that the raft state machine received
  // an acknowledgment of the heartbeat that attached with the read only request
  // context.
  size_t recvAck(const idl::Message &m);

  // advance advances the read only request queue kept by the readonly struct.
  // It dequeues the requests until it finds the read only request that has
  // the same context as the given `m`.
  std::vector<ReadIndexStatus> advance(const yaraft::idl::Message &m);

  // lastPendingRequestCtx returns the context of the last pending read only
  // request in readonly struct.
  std::string lastPendingRequestCtx();
};

inline std::unique_ptr<ReadOnly> newReadOnly(ReadOnlyOption option) {
  return std::unique_ptr<ReadOnly>(new ReadOnly(option));
}

}  // namespace yaraft
