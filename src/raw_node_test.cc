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

#include "raw_node.h"
#include "ready.h"
#include "test_utils.h"

using namespace yaraft;

class RawNodeTest : public BaseTest {};

// This test ensures that RawNode.Step ignore local message.
TEST_F(RawNodeTest, Step) {
  for (int i = pb::MessageType_MIN; i <= pb::MessageType_MAX; i++) {
    RawNode rn(newTestConfig(1, {1}, 10, 1, new MemoryStorage()));
    auto s = rn.Step(PBMessage().From(1).To(1).Type(static_cast<pb::MessageType>(i)).v);

    if (IsLocalMessage(pb::MessageType(i))) {
      ASSERT_EQ(s.Code(), Error::StepLocalMsg);
    } else {
      ASSERT_EQ(s.Code(), Error::OK);
    }
  }
}

TEST_F(RawNodeTest, Propose) {
  auto memstore = new MemoryStorage();
  RawNode rn(newTestConfig(1, {1}, 10, 1, memstore));
  rn.Campaign();
  ASSERT_EQ(rn.GetInfo().currentTerm, 1);
  ASSERT_EQ(rn.GetInfo().currentLeader, 1);

  for (int i = 0; i < 4; i++) {
    rn.Propose("a");
  }

  auto rd = rn.GetReady();
  ASSERT_TRUE(rd->hardState);
  ASSERT_TRUE(rd->hardState->has_commit());
  ASSERT_EQ(rd->hardState->commit(), 5);
  ASSERT_TRUE(rd->hardState->has_term());
  ASSERT_EQ(rd->hardState->term(), 1);

  rd->Advance(memstore);

  ASSERT_TRUE(rd->IsEmpty());

  // one no-op for election, 4 proposed logs
  ASSERT_EQ(rn.GetInfo().commitIndex, 5);
  ASSERT_EQ(rn.GetInfo().logIndex, 5);
}

TEST_F(RawNodeTest, ProposeConfChange) {
  auto memstore = new MemoryStorage;
  RawNode rn(newTestConfig(1, {1}, 10, 1, memstore));
  ASSERT_OK(rn.Campaign());
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(rn.Propose("a"));
  }

  for (uint64_t i = 2; i <= 5; i++) {
    auto cc = PBConfChange().Type(pb::ConfChangeAddNode).NodeId(i).v;
    ASSERT_OK(rn.ProposeConfChange(cc));

    Ready* rd = rn.GetReady();
    rd->Advance(memstore);
    rn.ApplyConfChange(cc);
  }

  {
    std::set<uint64_t> actual;
    auto pr = rn.GetInfo().progress;
    for (auto& e : pr) {
      actual.insert(e.first);
    }
    ASSERT_EQ(actual, std::set<uint64_t>({1, 2, 3, 4, 5}));
  }

  {
    EntryVec actual(memstore->TEST_Entries().begin() + 1, memstore->TEST_Entries().end());
    ASSERT_EQ(actual.size(), 9);

    for (int i = 0; i < 5; i++) {
      ASSERT_EQ(actual[i].type(), pb::EntryNormal);
    }

    for (int i = 5; i < 9; i++) {
      ASSERT_EQ(actual[i].type(), pb::EntryConfChange);
    }
  }
}

// TestRawNodeProposeAddDuplicateNode ensures that two proposes to add the same node should
// not affect the later propose to add new node.
TEST_F(RawNodeTest, ProposeAddDuplicateNode) {
  auto memstore = new MemoryStorage;
  RawNode rn(newTestConfig(1, {1}, 10, 1, memstore));
  ASSERT_OK(rn.Campaign());

  auto proposeConfChangeAndApply = [&](uint64_t nodeId) {
    ASSERT_OK(rn.ProposeConfChange(PBConfChange().Type(pb::ConfChangeAddNode).NodeId(nodeId).v));

    Ready* rd = rn.GetReady();
    for (const auto& e : rd->entries) {
      if (e.type() == pb::EntryConfChange) {
        pb::ConfChange cc;
        cc.ParseFromString(e.data());
        rn.ApplyConfChange(cc);
      }
    }

    rd->Advance(memstore);
    delete rd;
  };

  proposeConfChangeAndApply(2);
  proposeConfChangeAndApply(2);
  proposeConfChangeAndApply(3);

  {
    std::set<uint64_t> actual;
    auto pr = rn.GetInfo().progress;
    for (auto& e : pr) {
      actual.insert(e.first);
    }
    ASSERT_EQ(actual, std::set<uint64_t>({1, 2, 3}));
  }

  {
    EntryVec actual(memstore->TEST_Entries().begin() + 1, memstore->TEST_Entries().end());

    ASSERT_EQ(actual.size(), 4);

    ASSERT_EQ(actual[0].type(), pb::EntryNormal);
    ASSERT_EQ(actual[1].type(), pb::EntryConfChange);
    ASSERT_EQ(actual[2].type(), pb::EntryConfChange);
    ASSERT_EQ(actual[3].type(), pb::EntryConfChange);
  }
}