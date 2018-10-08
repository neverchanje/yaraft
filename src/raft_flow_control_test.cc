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

#include "inflights.h"
#include "raft.h"
#include "test_utils.h"

namespace yaraft {

class RaftFlowControlTest : public BaseTest {
 public:
  // TestMsgAppFlowControlFull ensures:
  // 1. msgApp can fill the sending window until full
  // 2. when the window is full, no more msgApp can be sent.
  void TestMsgAppFlowControlFull() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();

    auto& pr2 = r->prs_[2];
    // force the progress to be in replicate state
    pr2->becomeReplicate();
    for (int i = 0; i < r->maxInflight_; i++) {
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
      r->readMessages();
    }

    // 1 is noop, 2 is the first proposal we just sent.
    // so we start with 2.
    for (uint64_t tt = 2; tt < r->maxInflight_; tt++) {
      // move forward the window
      r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(tt));
      r->readMessages();

      // fill in the inflights window again
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
      auto ms = r->readMessages();
      ASSERT_EQ(ms.size(), 1);

      ASSERT_TRUE(pr2->ins_->full());

      // ensure 2
      for (uint64_t i = 0; i < tt; i++) {
        r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(i));
        ASSERT_TRUE(pr2->ins_->full());
      }
    }
  }

  // TestMsgAppFlowControlRecvHeartbeat ensures a heartbeat response
  // frees one slot if the window is full.
  void TestMsgAppFlowControlRecvHeartbeat() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();

    auto& pr2 = r->prs_[2];
    // force the progress to be in replicate state
    pr2->becomeReplicate();
    // fill in the inflights window
    for (int i = 0; i < r->maxInflight_; i++) {
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
      r->readMessages();
    }

    for (uint64_t tt = 1; tt < 5; tt++) {
      ASSERT_TRUE(pr2->ins_->full());

      // recv tt msgHeartbeatResp and expect one free slot
      for (uint64_t i = 0; i < tt; i++) {
        r->Step(idl::Message().from(2).to(1).type(idl::MsgHeartbeatResp));
        r->readMessages();
        ASSERT_FALSE(pr2->ins_->full());
      }

      // one slot
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
      auto ms = r->readMessages();
      ASSERT_EQ(ms.size(), 1);

      // and just one slot
      for (uint64_t i = 0; i < 10; i++) {
        r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
        auto ms1 = r->readMessages();
        ASSERT_EQ(ms1.size(), 0);
      }

      // clear all pending messages.
      r->Step(idl::Message().from(2).to(1).type(idl::MsgHeartbeatResp));
      r->readMessages();
    }
  }
};

TEST_F(RaftFlowControlTest, MsgAppFlowControlFull) {
  TestMsgAppFlowControlFull();
}

TEST_F(RaftFlowControlTest, MsgAppFlowControlRecvHeartbeat) {
  TestMsgAppFlowControlRecvHeartbeat();
}

}  // namespace yaraft
