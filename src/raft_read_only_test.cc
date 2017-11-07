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

#include "memory_storage.h"
#include "raft.h"
#include "test_utils.h"

namespace yaraft {

class RaftTest : public BaseTest {
 public:
  static void TestReadOnlyOptionSafe() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage);
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage);
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage);

    std::unique_ptr<Network> nt(Network::New(3));
    nt->Set(a)->Set(b)->Set(c);
    b->randomizedElectionTimeout_++;

    for (int i = 0; i < b->randomizedElectionTimeout_; i++) {
      b->Tick();
    }
    nt->StartElection(1);
    ASSERT_EQ(a->role_, Raft::StateRole::kLeader);

    struct TestData {
      Raft* sm;
      int proposals;
      uint64_t wri;
      std::string wctx;
    } tests[] = {
        {a, 10, 11, "ctx1"}, {a, 10, 21, "ctx2"},
    };

    for (auto tt : tests) {
      for (int j = 0; j < tt.proposals; j++) {
        nt->Propose(1, "");
      }
      nt->ReadIndex(tt.sm->id_, tt.wctx);

      auto r = tt.sm;
      ASSERT_EQ(r->readStates_.size(), 1);

      ASSERT_EQ(r->readStates_[0].index, tt.wri);
      ASSERT_EQ(r->readStates_[0].requestCtx, tt.wctx);

      r->readStates_.clear();
    }
  }

  // TestReadOnlyForNewLeader ensures that a leader only accepts MsgReadIndex message
  // when it commits at least one log entry at it term.
  static void TestReadOnlyForNewLeader() {
    struct NodeConfig {
      uint64_t id;
      uint64_t committed;
      uint64_t applied;
      uint64_t compactedIndex;
    } nodeConfigs[] = {
        {1, 1, 1, 0}, {2, 2, 2, 2}, {3, 2, 2, 2},
    };

    std::unique_ptr<Network> net(Network::New(3));
    for (auto& c : nodeConfigs) {
      auto storage = new MemoryStorage;
      storage->Append({PBEntry().Term(1).Index(1).v, PBEntry().Term(1).Index(2).v});
      storage->SetHardState(PBHardState().Commit(c.committed).Term(1).v);
      if (c.compactedIndex != 0) {
        storage->Compact(c.compactedIndex);
      }

      auto r = newTestRaft(c.id, {1, 2, 3}, 10, 1, storage);
      net->Set(r);
    }

    Raft* p1 = net->Peer(1);

    // Drop MsgApp to forbid peer a to commit any log entry at its term after it becomes leader.
    net->Ignore(pb::MsgApp);
    net->StartElection(1);
    ASSERT_EQ(p1->role_, Raft::kLeader);

    // Ensure p1 drops read only request.
    net->ReadIndex(1, "ctx");
    ASSERT_EQ(p1->readStates_.size(), 0);

    net->Recover();

    // Force p1 to commit a log entry at its term
    for (int i = 0; i < p1->c_->heartbeatTick; i++) {
      p1->Tick();
    }
    net->Propose(1, "");
    ASSERT_EQ(p1->log_->CommitIndex(), 4);

    uint64_t lastLogTerm = p1->log_->ZeroTermOnErrCompacted(p1->log_->CommitIndex());
    ASSERT_EQ(lastLogTerm, p1->Term());

    std::string ctx = "ctx";
    net->ReadIndex(1, ctx);
    ASSERT_EQ(p1->readStates_.size(), 1);
    ASSERT_EQ(p1->readStates_[0].index, 4);
    ASSERT_EQ(p1->readStates_[0].requestCtx, ctx);
  }
};

}  // namespace yaraft

using namespace yaraft;

TEST_F(RaftTest, TestReadOnlyOptionSafe) {
  RaftTest::TestReadOnlyOptionSafe();
}

TEST_F(RaftTest, TestReadOnlyForNewLeader) {
  RaftTest::TestReadOnlyForNewLeader();
}