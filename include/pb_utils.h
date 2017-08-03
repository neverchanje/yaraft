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

#include <ostream>

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

typedef std::vector<pb::Entry> EntryVec;

inline bool IsLocalMsg(const pb::Message& m) {
  if(!m.has_from()) {
    return true;
  }
  return m.from() == m.to();
}

inline bool IsResponseMsg(const pb::Message& m) {
  switch (m.type()) {
    case pb::MsgAppResp:
    case pb::MsgVoteResp:
    case pb::MsgPreVoteResp:
    case pb::MsgHeartbeatResp:
      return true;
    default:
      return false;
  }
}

pb::MessageType GetResponseType(pb::MessageType type);

// Print the message in a single line, useful for logging or other purposes.
std::string DumpPB(const google::protobuf::Message& msg);

inline std::ostream& operator<<(std::ostream& os, const google::protobuf::Message& msg) {
  return os << DumpPB(msg);
}

inline std::ostream& operator<<(std::ostream& os, const EntryVec& v) {
  os << "Size of entry vec: " << v.size() << ". | ";
  for (const auto& e : v) {
    os << DumpPB(e) << " | ";
  }
  return os << "\n";
}

}  // namespace yaraft
