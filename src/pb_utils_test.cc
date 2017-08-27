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

#include "pb_utils.h"

#include <gtest/gtest.h>

using namespace yaraft;

TEST(Util, IsLocalMessage) {
  struct TestData {
    pb::MessageType msgt;

    bool isLocal;
  } tests[] = {
      {pb::MsgHup, true},          {pb::MsgBeat, true},           {pb::MsgUnreachable, true},
      {pb::MsgSnapStatus, true},   {pb::MsgCheckQuorum, true},    {pb::MsgTransferLeader, false},
      {pb::MsgProp, false},        {pb::MsgApp, false},           {pb::MsgAppResp, false},
      {pb::MsgVote, false},        {pb::MsgVoteResp, false},      {pb::MsgSnap, false},
      {pb::MsgHeartbeat, false},   {pb::MsgHeartbeatResp, false}, {pb::MsgTimeoutNow, false},
      {pb::MsgReadIndex, false},   {pb::MsgReadIndexResp, false}, {pb::MsgPreVote, false},
      {pb::MsgPreVoteResp, false},
  };

  for (auto t : tests) {
    ASSERT_EQ(IsLocalMessage(t.msgt), t.isLocal);
  }
}
