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

#include "logging.h"
#include "read_only.h"

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

void ReadOnly::AddRequest(uint64_t idx, const pb::Message &m) {
  std::string ctx = m.entries(0).data();
  if (pendingReadIndex.find(ctx) != pendingReadIndex.end()) {
    return;
  }

  ReadIndexStatus rdIdx;
  rdIdx.index = idx;

  pendingReadIndex[ctx] = rdIdx;
  readIndexQueue.emplace_back(ctx);
}

int ReadOnly::RecvAck(const pb::Message &m) {
  auto it = pendingReadIndex.find(m.context());
  if (it == pendingReadIndex.end()) {
    return 0;
  }

  it->second.acks.insert(m.from());

  // add one to include an ack from local node
  return static_cast<int>(it->second.acks.size());
}

void ReadOnly::Advance(const pb::Message &m, std::vector<ReadState> *readStates) {
  int i = 0;
  bool found = false;
  const std::string &ctx = m.context();

  for (; i < readIndexQueue.size(); i++) {
    const std::string &okctx = readIndexQueue[i];

    // TODO(wutao1): Remove this when code is stable
    DLOG_ASSERT_S(pendingReadIndex.find(okctx) != pendingReadIndex.end(),
                  "cannot find corresponding read state from pending map")

    if (ctx == okctx) {
      found = true;
      break;
    }
  }

  if (found) {
    // assert readIndexQueue[i] == m.context()
    for (int k = 0; k <= i; k++) {
      ReadState state;
      state.requestCtx = std::move(readIndexQueue[k]);

      auto it = pendingReadIndex.find(state.requestCtx);
      state.index = it->second.index;

      readStates->emplace_back(std::move(state));
      pendingReadIndex.erase(it);
      readIndexQueue.pop_front();
    }
  }
}

}  // namespace yaraft