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

#include <cstdint>

#include <yaraft/conf.h>
#include <yaraft/idl_wrapper.h>
#include <yaraft/read_only.h>

#include "make_unique.h"

namespace yaraft {

void ReadOnly::addRequest(uint64_t idx, yaraft::idl::Message m) {
  std::string ctx = m.entries()[0].data();
  if (pendingReadIndex.find(ctx) != pendingReadIndex.end()) {
    return;
  }
  ReadIndexStatus rdIdx;
  rdIdx.index = idx;
  rdIdx.req = std::move(m);
  pendingReadIndex[ctx] = std::move(rdIdx);
  readIndexQueue.push_back(std::move(ctx));
}

size_t ReadOnly::recvAck(const yaraft::idl::Message &m) {
  auto it = pendingReadIndex.find(m.context());
  if (it == pendingReadIndex.end()) {
    return 0;
  }

  it->second.acks.insert(m.from());
  // add one to include an ack from local node
  return it->second.acks.size() + 1;
}

std::vector<ReadIndexStatus> ReadOnly::advance(const yaraft::idl::Message &m) {
  size_t i = 0;
  bool found = false;

  const std::string &ctx = m.context();
  std::vector<ReadIndexStatus> rss;

  for (const std::string &okctx : readIndexQueue) {
    i++;

    auto it = pendingReadIndex.find(okctx);
    if (it == pendingReadIndex.end()) {
      RAFT_PANIC("cannot find corresponding read state from pending map");
    }
    rss.push_back(std::move(it->second));
    if (ctx == okctx) {
      found = true;
      break;
    }
  }

  if (found) {
    readIndexQueue.SliceFrom(i);
    for (ReadIndexStatus rs : rss) {
      pendingReadIndex.erase(rs.req.entries()[0].data());
    }
    return rss;
  }

  return {};
}

std::string ReadOnly::lastPendingRequestCtx() {
  if (readIndexQueue.empty()) {
    return "";
  }
  return readIndexQueue[readIndexQueue.size() - 1];
}

}  // namespace yaraft
