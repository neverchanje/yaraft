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

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace yaraft {

bool IsLocalMsg(pb::Message& m) {
  return m.from() == m.to();
}

bool IsResponseMsg(pb::Message& m) {
  switch (m.type()) {
    case pb::MsgAppResp:
    case pb::MsgVoteResp:
    case pb::MsgPreVoteResp:
      return true;
    default:
      return false;
  }
}

// Print the message in a single line, useful for logging or other purposes.
std::string DumpPB(const google::protobuf::Message& msg) {
  std::string msgstr = msg.DebugString();
  boost::trim(msgstr);

  std::vector<std::string> tmp;
  boost::split(tmp, msgstr, [](char c) { return c == '\n'; });
  msgstr = boost::join(tmp, ", ");
  return std::string("{") + msgstr + '}';
}

std::ostream& operator<<(std::ostream& os, const google::protobuf::Message& msg) {
  return os << DumpPB(msg);
}

}  // namespace yaraft
