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

#include <yaraft/raw_node.h>
#include <yaraft/ready.h>

#include "test_utils.h"

namespace yaraft {

class RawNodeTest : public BaseTest {
 public:
  // TestRawNodeStep ensures that RawNode.Step ignore local message.
  void TestRawNodeStep() {
    for (int i = 0; i < idl::NUM_MSG_TYPES; i++) {
      RawNodeUPtr rn(RawNode::New(newTestConfig(1, {}, 10, 1, new MemoryStorage()), {{1}}));

      auto msgt = static_cast<idl::MessageType>(i);
      auto err = rn->Step(idl::Message().type(msgt));
      // LocalMsg should be ignored.
      if (IsLocalMsg(msgt)) {
        ASSERT_EQ(err, ErrStepLocalMsg);
      }
    }
  }

  // TestRawNodeProposeAndConfChange ensures that RawNode.Propose and RawNode.ProposeConfChange
  // send the given proposal and ConfChange to the underlying raft.
  void TestRawNodeProposeAndConfChange() {
    auto s = new MemoryStorage;
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    ReadyUPtr rd = rawNode->GetReady();
    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));

    rawNode->Campaign();

    bool proposed = false;
    uint64_t lastIndex;
    std::string ccdata;
    while (true) {
      rd = rawNode->GetReady();
      s->Append(rd->entries);
      // Once we are the leader, propose a command and a ConfChange.
      if (!proposed && rd->softState->lead == rawNode->raft_->id_) {
        rawNode->Propose("somedata");

        auto cc = idl::ConfChange().type(idl::ConfChangeAddNode).nodeid(1);
        ccdata = cc.Marshal();
        rawNode->ProposeConfChange(cc);
        proposed = true;
      }
      rawNode->Advance(std::move(rd));

      // Exit when we have four entries: one ConfChange, one no-op for the election,
      // our proposed command and proposed ConfChange.
      lastIndex = s->LastIndex().get_value();
      if (lastIndex >= 4) {
        break;
      }
    }

    auto entries = s->Entries(lastIndex - 1, lastIndex + 1, noLimit).get_value();
    ASSERT_EQ(entries.size(), 2);
    ASSERT_EQ(entries[0].data(), "somedata");
    ASSERT_EQ(entries[1].type(), idl::EntryConfChange);
    ASSERT_EQ(entries[1].data(), ccdata);
  }

  // TestRawNodeProposeAddDuplicateNode ensures that two proposes to add the same node should
  // not affect the later propose to add new node.
  void TestRawNodeProposeAddDuplicateNode() {
    auto s = new MemoryStorage;
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    ReadyUPtr rd = rawNode->GetReady();
    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));

    rawNode->Campaign();

    while (true) {
      rd = rawNode->GetReady();
      s->Append(rd->entries);

      if (rd->softState->lead == rawNode->raft_->id_) {
        rawNode->Advance(std::move(rd));
        break;
      }
      rawNode->Advance(std::move(rd));
    }

    auto proposeConfChangeAndApply = [&](idl::ConfChange cc) {
      rawNode->ProposeConfChange(cc);
      rd = rawNode->GetReady();
      s->Append(rd->entries);
      for (const auto& entry : rd->committedEntries) {
        if (entry.type() == idl::EntryConfChange) {
          idl::ConfChange cc2;
          cc2.Unmarshal(entry.data());
          rawNode->ApplyConfChange(cc2);
        }
      }
      rawNode->Advance(std::move(rd));
    };

    auto cc1 = idl::ConfChange().type(idl::ConfChangeAddNode).nodeid(1);
    auto ccdata1 = cc1.Marshal();

    proposeConfChangeAndApply(cc1);

    // try to add the same node again
    proposeConfChangeAndApply(cc1);

    // the new node join should be ok
    auto cc2 = idl::ConfChange().type(idl::ConfChangeAddNode).nodeid(2);
    auto ccdata2 = cc2.Marshal();
    proposeConfChangeAndApply(cc2);

    uint64_t lastIndex = s->LastIndex().get_value();
    // the last three entries should be: ConfChange cc1, cc1, cc2
    auto entries = s->Entries(lastIndex - 2, lastIndex + 1, noLimit).get_value();
    ASSERT_EQ(entries.size(), 3);
    ASSERT_EQ(entries[0].data(), ccdata1);
    ASSERT_EQ(entries[2].data(), ccdata2);
  }

  // TestRawNodeReadIndex ensures that Rawnode.ReadIndex sends the MsgReadIndex message
  // to the underlying raft. It also ensures that ReadState can be read out.
  void TestRawNodeReadIndex() {
    std::vector<idl::Message> msgs;

    auto appendStep = [&](idl::Message m) -> error_s {
      msgs.push_back(m);
      return error_s::ok();
    };

    std::vector<ReadState> wrs{ReadState().Index(1).RequestCtx("somedata")};

    auto s = new MemoryStorage;
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    rawNode->raft_->readStates_ = wrs;
    // ensure the ReadStates can be read out
    ASSERT_TRUE(rawNode->HasReady());
    ReadyUPtr rd = rawNode->GetReady();
    ASSERT_EQ(rd->readStates, wrs);

    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));
    // ensure raft.readStates is reset after advance
    ASSERT_TRUE(rawNode->raft_->readStates_.empty());

    std::string wrequestCtx = "somedata2";
    rawNode->Campaign();
    while (true) {
      rd = rawNode->GetReady();
      s->Append(rd->entries);

      if (rd->softState->lead == rawNode->raft_->id_) {
        rawNode->Advance(std::move(rd));

        // Once we are the leader, issue a ReadIndex request

        rawNode->raft_->step_ = appendStep;
        rawNode->ReadIndex(wrequestCtx);
        break;
      }
      rawNode->Advance(std::move(rd));
    }
    // ensure that MsgReadIndex message is sent to the underlying raft
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgReadIndex);
    ASSERT_EQ(msgs[0].entries()[0].data(), wrequestCtx);
  }

  // TestRawNodeStart ensures that a node can be started correctly. The node should
  // start with correct configuration change entries, and can accept and commit
  // proposals.
  void TestRawNodeStart() {
    auto cc = idl::ConfChange().type(idl::ConfChangeAddNode).nodeid(1);
    auto ccdata = cc.Marshal();

    Ready wants[2];

    wants[0].hardState = idl::HardState().term(1).commit(1).vote(0);
    wants[0].entries = idl::EntryVec{
        idl::Entry().type(idl::EntryConfChange).term(1).index(1).data(ccdata),
    };
    wants[0].committedEntries = idl::EntryVec{
        idl::Entry().type(idl::EntryConfChange).term(1).index(1).data(ccdata),
    };
    wants[0].mustSync = true;

    wants[1].hardState = idl::HardState().term(2).commit(3).vote(1);
    wants[1].entries = idl::EntryVec{
        idl::Entry().term(2).index(3).data("foo"),
    };
    wants[1].committedEntries = idl::EntryVec{
        idl::Entry().term(2).index(3).data("foo"),
    };
    wants[1].mustSync = true;

    auto s = new MemoryStorage;
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    ReadyUPtr rd = rawNode->GetReady();
    assertReadyEqual(*rd, wants[0]);
    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));

    rawNode->Campaign();
    rd = rawNode->GetReady();
    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));

    rawNode->Propose("foo");
    rd = rawNode->GetReady();
    assertReadyEqual(*rd, wants[1]);
    s->Append(rd->entries);
    rawNode->Advance(std::move(rd));
    ASSERT_FALSE(rawNode->HasReady());
  }

  void TestRawNodeRestart() {
    idl::EntryVec entries{
        idl::Entry().index(1).term(1),
        idl::Entry().index(2).term(1).data("foo"),
    };
    auto st = idl::HardState().term(1).commit(1);

    Ready want;
    // commit up to commit index in st
    entries.SliceTo(st.commit());
    want.committedEntries = entries;
    want.mustSync = true;

    auto s = new MemoryStorage;
    s->Append(entries);
    s->SetHardState(st);
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    ReadyUPtr rd = rawNode->GetReady();
    assertReadyEqual(*rd, want);

    rawNode->Advance(std::move(rd));
    ASSERT_FALSE(rawNode->HasReady());
  }

  void TestRawNodeRestartFromSnapshot() {
    auto snap = idl::Snapshot().metadata_index(2).metadata_term(1).metadata_conf_state(idl::ConfState().nodes({1, 2}));
    idl::EntryVec entries{
        idl::Entry().index(3).term(1).data("foo"),
    };
    auto st = idl::HardState().term(1).commit(3);

    Ready want;
    // commit up to commit index in st
    want.committedEntries = entries;
    want.mustSync = true;

    auto s = new MemoryStorage;
    s->SetHardState(st);
    s->ApplySnapshot(snap);
    s->Append(entries);
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, s), {{1}}));

    ReadyUPtr rd = rawNode->GetReady();
    assertReadyEqual(*rd, want);

    rawNode->Advance(std::move(rd));
    ASSERT_FALSE(rawNode->HasReady());
  }

  void TestRawNodeStatus() {
    auto storage = new MemoryStorage;
    RawNodeUPtr rawNode(RawNode::New(newTestConfig(1, {}, 10, 1, storage), {{1}}));
    auto status = rawNode->GetStatus();
  }

  void assertReadyEqual(const Ready& r1, const Ready& r2) {
    ASSERT_EQ(bool(r1.softState), bool(r2.softState));
    if (r1.softState && r2.softState) {
      ASSERT_EQ(*r1.softState, *r2.softState);
    }
    ASSERT_EQ(r1.hardState, r2.hardState);
    EntryVec_ASSERT_EQ(r1.committedEntries, r2.committedEntries);
    EntryVec_ASSERT_EQ(r1.entries, r2.entries);
    ASSERT_EQ(r1.readStates, r2.readStates);
    ASSERT_EQ(r1.messages, r2.messages);
    ASSERT_EQ(r1.snapshot, r2.snapshot);
    ASSERT_EQ(r1.mustSync, r2.mustSync);
  }
};

TEST_F(RawNodeTest, Step) {
  TestRawNodeStep();
}

TEST_F(RawNodeTest, ProposeConfChange) {
  TestRawNodeProposeAndConfChange();
}

TEST_F(RawNodeTest, ProposeAddDuplicateNode) {
  TestRawNodeProposeAddDuplicateNode();
}

TEST_F(RawNodeTest, ReadIndex) {
  TestRawNodeReadIndex();
}

TEST_F(RawNodeTest, Start) {
  TestRawNodeStart();
}

TEST_F(RawNodeTest, Restart) {
  TestRawNodeRestart();
}

TEST_F(RawNodeTest, RestartFromSnapshot) {
  TestRawNodeRestartFromSnapshot();
}

}  // namespace yaraft
