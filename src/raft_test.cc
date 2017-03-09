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

#include "raft.h"
#include "memory_storage.h"
#include "pb_helper.h"
#include "test_utils.h"

#include <gtest/gtest.h>

namespace yaraft {

class RaftTest {
 public:
  // Ensure that the Step function ignores the message from old term and does not pass it to the
  // actual stepX function.
  static void TestStepIgnoreOldTermMsg() {
    RaftUPtr raft(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));

    bool called = false;
    raft->step_ = [&](const pb::Message &m) { called = true; };

    raft->currentTerm_ = 2;

    pb::Message m;
    m.set_term(raft->Term() - 1);
    m.set_type(pb::MsgApp);
    raft->Step(m);
    ASSERT_FALSE(called);
  }

  // HandleAppendEntries ensures:
  // 1. Reply false if log doesn’t contain an entry at prevLogIndex whose term matches prevLogTerm.
  // 2. If an existing entry conflicts with a new one (same index but different terms),
  //    delete the existing entry and all that follow it; append any new entries not already in the
  //    log.
  // 3. If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry).
  static void TestHandleMsgApp() {
    struct TestData {
      pb::Message m;

      uint64_t wIndex;
      uint64_t wCommit;
      bool wReject;
    } tests[] = {
        // Ensure 1
        // previous log mismatch
        {PBMessage().Type(pb::MsgApp).Term(2).LogTerm(3).Index(2).Commit(3).v, 2, 0, true},
        // previous log non-exist
        {PBMessage().Type(pb::MsgApp).Term(2).LogTerm(3).Index(3).Commit(3).v, 2, 0, true},

        // Ensure 2
        {PBMessage().Type(pb::MsgApp).Term(2).LogTerm(1).Index(1).Commit(1).v, 2, 1, false},
        {PBMessage()
             .Type(pb::MsgApp)
             .Term(2)
             .LogTerm(0)
             .Index(0)
             .Commit(1)
             .Entries({pbEntry(1, 2)})
             .v,
         1, 1, false},
        {PBMessage()
             .Type(pb::MsgApp)
             .Term(2)
             .LogTerm(2)
             .Index(2)
             .Commit(3)
             .Entries(pbEntry(3, 2) + pbEntry(4, 2))
             .v,
         4, 3, false},
        {PBMessage()
             .Type(pb::MsgApp)
             .Term(2)
             .LogTerm(2)
             .Index(2)
             .Commit(4)
             .Entries({pbEntry(3, 2)})
             .v,
         3, 3, false},
        {PBMessage()
             .Type(pb::MsgApp)
             .Term(2)
             .LogTerm(1)
             .Index(1)
             .Commit(4)
             .Entries({pbEntry(2, 2)})
             .v,
         2, 2, false},

        // Ensure 3
        // match entry 1, commit up to last new entry 1
        {PBMessage().Type(pb::MsgApp).Term(1).LogTerm(1).Index(1).Commit(3).v, 2, 1, false},
        // match entry 1, commit up to last new entry 2
        {PBMessage()
             .Type(pb::MsgApp)
             .Term(1)
             .LogTerm(1)
             .Index(1)
             .Commit(3)
             .Entries({pbEntry(2, 2)})
             .v,
         2, 2, false},
        // match entry 2, commit up to last new entry 2
        {PBMessage().Type(pb::MsgApp).Term(2).LogTerm(2).Index(2).Commit(3).v, 2, 2, false},
        // commit up to log.last()
        {PBMessage().Type(pb::MsgApp).Term(2).LogTerm(2).Index(2).Commit(4).v, 2, 2, false},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(EntryVec({pbEntry(1, 1), pbEntry(2, 2)}));
      RaftUPtr raft(newTestRaft(1, {1}, 10, 1, storage));
      raft->becomeFollower(2, 0);

      raft->handleAppendEntries(t.m);
      ASSERT_EQ(raft->log_->LastIndex(), t.wIndex);
      ASSERT_EQ(raft->log_->CommitIndex(), t.wCommit);
      ASSERT_EQ(raft->mails_.size(), 1);
      ASSERT_EQ(raft->mails_.begin()->reject(), t.wReject);
    }
  }

  static void TestStateTransition() {
    struct TestData {
      Raft::StateRole from;
      Raft::StateRole to;

      bool wallow;
      uint64_t wterm;
      uint64_t wlead;
    } tests[] = {
        {Raft::kFollower, Raft::kFollower, true, 1, 0},
        {Raft::kFollower, Raft::kCandidate, true, 1, 0},
        {Raft::kFollower, Raft::kLeader, false, 0, 0},

        {Raft::kCandidate, Raft::kFollower, true, 0, 0},
        {Raft::kCandidate, Raft::kCandidate, true, 1, 0},
        {Raft::kCandidate, Raft::kLeader, true, 0, 1},

        {Raft::kLeader, Raft::kFollower, true, 1, 0},
        {Raft::kLeader, Raft::kCandidate, false, 1, 0},

        // TODO: Is it really allowed to convert leader to leader?
        {Raft::kLeader, Raft::kLeader, true, 0, 1},
    };

    for (auto t : tests) {
      auto raft = newTestRaft(1, {1}, 10, 1, new MemoryStorage());
      raft->role_ = t.from;

      bool failed = false;
      try {
        switch (t.to) {
          case Raft::kFollower:
            raft->becomeFollower(t.wterm, t.wlead);
            break;
          case Raft::kCandidate:
            raft->becomeCandidate();
            break;
          case Raft::kLeader:
            raft->becomeLeader();
            break;
        }
      } catch (RaftError &e) {
        failed = true;
      }
      ASSERT_EQ(!t.wallow, failed);

      if (t.wallow) {
        ASSERT_EQ(raft->currentTerm_, t.wterm);
        ASSERT_EQ(raft->currentLeader_, t.wlead);
      }
    }
  }

  static void TestHandleHeartbeat() {
    uint64_t commit = 2;

    struct TestData {
      pb::Message m;

      uint64_t wCommit;
    } tests[] = {
        // do not decrease commit
        {PBMessage().From(2).To(1).Type(pb::MsgHeartbeat).Term(2).Commit(commit - 1).v, commit},

        {PBMessage().From(2).To(1).Type(pb::MsgHeartbeat).Term(2).Commit(commit + 1).v, commit + 1},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append({pbEntry(1, 1), pbEntry(2, 2), pbEntry(3, 3)});
      RaftUPtr raft(newTestRaft(1, {1, 2}, 10, 1, storage));
      raft->becomeFollower(2, 0);
      raft->log_->CommitTo(commit);

      raft->handleHeartbeat(t.m);

      ASSERT_EQ(raft->log_->CommitIndex(), t.wCommit);
    }
  }
};

}  // namespace yaraft

TEST(Raft, StepIgnoreOldTermMsg) {
  yaraft::RaftTest::TestStepIgnoreOldTermMsg();
}

TEST(Raft, HandleAppendEntries) {
  yaraft::RaftTest::TestHandleMsgApp();
}

TEST(Raft, StateTransition) {
  yaraft::RaftTest::TestStateTransition();
}

TEST(Raft, HandleHeartbeat) {
  yaraft::RaftTest::TestHandleHeartbeat();
}