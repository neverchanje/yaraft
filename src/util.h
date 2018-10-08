// Copyright 17 The etcd Authors
// Copyright 17 Wu Tao
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

#include <yaraft/idl_wrapper.h>
#include <yaraft/logger.h>
#include <yaraft/span.h>

namespace yaraft {

extern idl::EntrySpan limitSize(uint64_t maxSize, idl::EntrySpan ents);

inline void limitSize(uint64_t maxSize, idl::EntryVec *entries) {
  idl::EntrySpan limited = limitSize(maxSize, *entries);
  entries->Slice(0, limited.size() - 1);
}

static constexpr uint64_t noLimit = std::numeric_limits<uint64_t>::max();

// voteResponseType maps vote and prevote message types to their corresponding responses.
inline idl::MessageType voteRespMsgType(idl::MessageType msgt) {
  switch (msgt) {
    case idl::MsgVote:
      return idl::MsgVoteResp;
    case idl::MsgPreVote:
      return idl::MsgPreVoteResp;
    default:
      RAFT_PANIC(fmt::sprintf("not a vote message: %s", msgt));
      __builtin_unreachable();
  }
}

}  // namespace yaraft
