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

  // TestHandleMsgApp ensures:
  // 1. Reply false if log doesn’t contain an entry at prevLogIndex whose term matches prevLogTerm.
  // 2. If an existing entry conflicts with a new one (same index but different terms),
  //    delete the existing entry and all that follow it; append any new entries not already in the
  //    log.
  // 3. If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry).
  static void TestHandleMsgApp() {
    struct TestData {
      uint64_t prevLogIndex, prevLogTerm;
      uint64_t commit;
      EntryVec ents;

      uint64_t wIndex;
      uint64_t wCommit;
      bool wReject;
    } tests[] = {
        // Ensure 1
        // previous log mismatch
        {3, 2, 3, {}, 2, 0, true},
        // previous log non-exist
        {3, 3, 3, {}, 2, 0, true},

        // Ensure 2
        {1, 1, 1, {}, 2, 1, false},
        {0, 0, 1, {pbEntry(1, 2)}, 1, 1, false},
        {2, 2, 3, {pbEntry(3, 2), pbEntry(4, 2)}, 4, 3, false},
        {2, 2, 4, {pbEntry(3, 2)}, 3, 3, false},
        {1, 1, 4, {pbEntry(2, 2)}, 2, 2, false},

        // Ensure 3
        // match entry 1, commit up to last new entry 1
        {1, 1, 3, {}, 2, 1, false},
        // match entry 1, commit up to last new entry 2
        {1, 1, 3, {pbEntry(2, 2)}, 2, 2, false},
        // match entry 2, commit up to last new entry 2
        {2, 2, 3, {}, 2, 2, false},
        // commit up to log.last()
        {2, 2, 4, {}, 2, 2, false},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(EntryVec({pbEntry(1, 1), pbEntry(2, 2)}));
      RaftUPtr raft(newTestRaft(1, {1}, 10, 1, storage));
      raft->becomeFollower(2, 0);

      raft->handleAppendEntries(PBMessage()
                                    .Type(pb::MsgApp)
                                    .Term(raft->currentTerm_)
                                    .LogTerm(t.prevLogTerm)
                                    .Index(t.prevLogIndex)
                                    .Commit(t.commit)
                                    .Entries(t.ents)
                                    .v);
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
      RaftUPtr raft(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));
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
          default:
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

  // TestHandleHeartbeatResp ensures that we re-send log entries when we get a heartbeat response.
  static void TestHandleHeartbeatResp() {
    RaftUPtr raft(newTestRaft(1, {1, 2}, 10, 1,
                              new MemoryStorage(pbEntry(1, 1) + pbEntry(2, 2) + pbEntry(3, 3))));
    raft->becomeCandidate();
    raft->becomeLeader();

    // A heartbeat response from a node that is behind; re-send MsgApp
    raft->Step(PBMessage().From(2).Type(pb::MsgHeartbeatResp).Term(1).v);
    ASSERT_EQ(raft->mails_.size(), 1);
    ASSERT_EQ(raft->mails_.begin()->type(), pb::MsgApp);

    // A second heartbeat response generates another MsgApp re-send
    raft->mails_.clear();
    raft->Step(PBMessage().From(2).Type(pb::MsgHeartbeatResp).Term(1).v);
    ASSERT_EQ(raft->mails_.size(), 1);
    ASSERT_EQ(raft->mails_.begin()->type(), pb::MsgApp);

    // Once we have an MsgAppResp that pushes MatchIndex forward, heartbeats no longer send MsgApp.
    auto msg = *raft->mails_.begin();
    raft->Step(
        PBMessage().From(2).Type(pb::MsgAppResp).Index(msg.index() + msg.entries_size()).Term(1).v);
    raft->mails_.clear();

    raft->Step(PBMessage().From(2).Type(pb::MsgHeartbeatResp).Term(1).v);
    ASSERT_EQ(raft->mails_.size(), 0);
  }

  static void TestLeaderElection(bool preVote) {
    struct TestData {
      Network *network;
      Raft::StateRole role;

      uint64_t wterm;
    } tests[] = {
        // three nodes, all healthy
        {Network::New(3), Raft::kLeader, 1},

        // three nodes, one sick
        {Network::New(3)->Down(2), Raft::kLeader, 1},

        // three nodes, two sick
        {Network::New(3)->Down(2)->Down(3), Raft::kCandidate, 1},

        // four nodes, two sick
        {Network::New(4)->Down(2)->Down(3), Raft::kCandidate, 1},

        // five nodes, two sick
        {Network::New(5)->Down(2)->Down(3), Raft::kLeader, 1},
    };

    for (auto t : tests) {
      t.network->SetPreVote(preVote);
      t.network->StartElection(1);

      auto node = t.network->Peer(1);

      if (preVote && t.role == Raft::kCandidate) {
        t.role = Raft::kPreCandidate;
        t.wterm = 0;
      }
      ASSERT_EQ(node->role_, t.role);

      ASSERT_EQ(node->currentTerm_, t.wterm);
      delete t.network;
    }
  }

  // TestLeaderCycle verifies that each node in a cluster can campaign
  // and be elected in turn. This ensures that elections (including
  // pre-vote) work when not starting from a clean slate (as they do in
  // TestLeaderElection)
  static void TestLeaderCycle(bool preVote) {
    for (uint64_t cand = 1; cand <= 1; cand++) {
      std::unique_ptr<Network> n(Network::New(3));
      n->SetPreVote(preVote);
      n->StartElection(cand);

      for (uint64_t id = 1; id <= 3; id++) {
        ASSERT_EQ(n->Peer(id)->role_, cand == id ? Raft::kLeader : Raft::kFollower);
      }
    }
  }

  static void TestCommit() {
    struct TestData {
      std::vector<uint64_t> matches;
      EntryVec logs;
      uint64_t smTerm;

      uint64_t wcommit;
    } tests[] = {

        /// single
        {{1}, {pbEntry(1, 1)}, 1, 1},
        {{1}, {pbEntry(1, 1)}, 2, 0},  // not commit in newer term
        {{2}, {pbEntry(1, 1), pbEntry(2, 2)}, 2, 2},
        {{1}, {pbEntry(1, 2)}, 2, 1},

        // odd
        {{2, 1, 1}, {pbEntry(1, 1), pbEntry(2, 1)}, 1, 1},
        {{2, 1, 1}, {pbEntry(1, 1), pbEntry(2, 1)}, 2, 0},
        {{2, 1, 2}, {pbEntry(1, 1), pbEntry(2, 2)}, 2, 2},
        {{2, 1, 2}, {pbEntry(1, 1), pbEntry(2, 1)}, 2, 0},

        // odd
        {{2, 1, 1, 1}, {pbEntry(1, 1), pbEntry(2, 2)}, 1, 1},
        {{2, 1, 1, 1}, {pbEntry(1, 1), pbEntry(2, 1)}, 2, 0},
        {{2, 1, 1, 2}, {pbEntry(1, 1), pbEntry(2, 2)}, 1, 1},
        {{2, 1, 1, 2}, {pbEntry(1, 1), pbEntry(2, 1)}, 2, 0},
        {{2, 1, 2, 2}, {pbEntry(1, 1), pbEntry(2, 2)}, 2, 2},
        {{2, 1, 2, 2}, {pbEntry(1, 1), pbEntry(2, 1)}, 2, 0},
    };

    for (auto t : tests) {
      RaftUPtr r(newTestRaft(1, {1}, 5, 1, new MemoryStorage(t.logs)));
      r->loadState(PBHardState().Term(t.smTerm).v);
      r->role_ = Raft::kLeader;

      for (int i = 0; i < t.matches.size(); i++) {
        uint64_t id = static_cast<uint64_t>(i + 1);
        r->prs_[id] = Progress().MatchIndex(t.matches[i]).NextIndex(t.matches[i] + 1);
      }
      r->advanceCommitIndex();
      ASSERT_EQ(r->log_->CommitIndex(), t.wcommit);
    }
  }

  // TestCampaignWhileLeader ensures that a leader node won't step down
  // when it elects itself.
  static void TestCampaignWhileLeader() {
    RaftUPtr r(newTestRaft(1, {1}, 5, 1, new MemoryStorage()));
    ASSERT_EQ(r->role_, Raft::kFollower);

    r->Step(PBMessage().From(1).To(1).Type(pb::MsgHup).Term(1).v);
    ASSERT_EQ(r->role_, Raft::kLeader);

    r->Step(PBMessage().From(1).To(1).Type(pb::MsgHup).Term(1).v);
    ASSERT_EQ(r->role_, Raft::kLeader);
  }

  static void TestDuelingCandidates() {
    std::unique_ptr<Network> n(Network::New(3));
    n->Cut(1, 3);
    n->StartElection(1);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);
    ASSERT_EQ(n->Peer(1)->log_->CommitIndex(), 1);
    ASSERT_EQ(n->Peer(2)->log_->LastIndex(), 1);
    ASSERT_EQ(n->Peer(3)->log_->LastIndex(), 0);

    // 3 stays as candidate since it receives a vote from 3 and a rejection from 2
    n->StartElection(3);
    ASSERT_EQ(n->Peer(3)->role_, Raft::kCandidate);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);
    ASSERT_EQ(n->Peer(2)->Term(), 1);

    n->Restore(1, 3);

    // candidate 3 now increases its term and tries to vote again
    // we expect it to disrupt the leader 1 since it has a higher term
    // 3 will be follower again since both 1 and 2 rejects its vote request
    // since 3 does not have a long enough log
    n->StartElection(3);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kFollower);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kFollower);
    ASSERT_EQ(n->Peer(3)->role_, Raft::kFollower);
  }

  static void TestDuelingPreCandidates() {
    std::unique_ptr<Network> n(Network::New(3));
    n->SetPreVote(true);
    n->Cut(1, 3);

    // 1 becomes leader since it receives votes from 1 and 2
    n->StartElection(1);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);

    // 3 campaigns then reverts to follower when its PreVote is rejected
    n->StartElection(3);
    ASSERT_EQ(n->Peer(3)->role_, Raft::kFollower);

    n->Restore(1, 3);

    // Candidate 3 now increases its term and tries to vote again.
    // With PreVote, it does not disrupt the leader.
    n->StartElection(3);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);
    ASSERT_EQ(n->Peer(2)->role_, Raft::kFollower);
    ASSERT_EQ(n->Peer(3)->role_, Raft::kFollower);
  }

  // TestVoteFromAnyState ensures that no matter what state a node is from,
  // it will always step down and vote for a legal candidate.
  static void TestVoteFromAnyState(pb::MessageType type) {
    for (int i = 0; i < Raft::kStateNum; i++) {
      auto role = Raft::StateRole(i);
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));

      if (role == Raft::kFollower) {
        r->becomeFollower(1, 3);
      } else if (role == Raft::kCandidate) {
        r->becomeCandidate();
      } else if (role == Raft::kLeader) {
        r->becomeCandidate();
        r->becomeLeader();
      } else if (role == Raft::kPreCandidate) {
        r->becomeFollower(1, 3);
        r->becomePreCandidate();
      }
      ASSERT_EQ(r->Term(), 1);

      uint64_t newTerm = 2;
      uint64_t from = 2;
      r->Step(PBMessage().From(from).To(1).Type(type).Term(newTerm).LogTerm(newTerm).Index(4).v);

      if (type == pb::MsgVote) {
        // If this was a real vote, we reset our state and term.
        ASSERT_EQ(r->mails_.size(), 1);
        ASSERT_EQ(r->mails_[0].type(), voteRespType(type));
        ASSERT_FALSE(r->mails_[0].reject());
        ASSERT_EQ(r->votedFor_, from);
        ASSERT_EQ(r->currentTerm_, newTerm);
        ASSERT_EQ(r->role_, Raft::kFollower);
      } else {
        // pre-vote doesn't change anything.
        if (r->role_ == Raft::kCandidate || r->role_ == Raft::kLeader) {
          ASSERT_EQ(r->votedFor_, r->id_);
        } else {
          ASSERT_EQ(r->votedFor_, 0);
        }
        ASSERT_EQ(r->currentTerm_, 1);
        ASSERT_EQ(r->role_, role);
      }
    }
  }

  static void TestSingleNodeCandidate() {
    std::unique_ptr<Network> n(Network::New(1));
    n->StartElection(1);
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);
  }

  static void TestSingleNodePreCandidate() {
    std::unique_ptr<Network> n(Network::New(1));
    n->StartElection(1);
    n->MutablePeerConfig(1)->preVote = true;
    ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);
  }

  static void TestSingleNodeCommit() {
    std::unique_ptr<Network> n(Network::New(1));
    n->StartElection(1);
    n->ReplicateAppend(1);

    n->Send(PBMessage().From(1).To(1).Type(pb::MsgProp).Entries({PBEntry().Data("somedata").v}).v);
    n->ReplicateAppend(1);

    n->Send(PBMessage().From(1).To(1).Type(pb::MsgProp).Entries({PBEntry().Data("somedata").v}).v);
    n->ReplicateAppend(1);

    ASSERT_EQ(n->Peer(1)->log_->CommitIndex(), 3);
  }

  static void TestLogReplication() {
    {
      std::unique_ptr<Network> n(Network::New(3));

      n->StartElection(1);
      ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);

      n->Send(PBMessage().Type(pb::MsgProp).From(1).To(1).Entries({PBEntry().Data("data").v}).v);
      n->ReplicateAppend(1);

      for (uint64_t i = 1; i <= 3; i++) {
        ASSERT_EQ(n->Peer(i)->log_->CommitIndex(), 2) << " " << i;
      }
    }

    {
      std::unique_ptr<Network> n(Network::New(3));

      n->StartElection(1);
      ASSERT_EQ(n->Peer(1)->role_, Raft::kLeader);

      n->Send(PBMessage().Type(pb::MsgProp).From(1).To(1).Entries({PBEntry().Data("data").v}).v);
      n->ReplicateAppend(1);

      n->StartElection(2);
      ASSERT_EQ(n->Peer(2)->role_, Raft::kLeader);

      n->Send(PBMessage().Type(pb::MsgProp).From(2).To(2).Entries({PBEntry().Data("data").v}).v);
      n->ReplicateAppend(2);

      for (uint64_t i = 1; i <= 3; i++) {
        ASSERT_EQ(n->Peer(i)->log_->CommitIndex(), 4);
      }
    }
  }

  static void TestProposalByProxy() {
    struct TestData {
      Network *network;
    } tests[] = {{Network::New(3)}, {Network::New(3)->Down(3)}};
    for (auto t : tests) {
      std::unique_ptr<Network> n(t.network);

      // index = 1
      n->StartElection(1);

      n->Send(
          PBMessage().From(2).To(2).Type(pb::MsgProp).Entries({PBEntry().Data("some data").v}).v);
      auto prop = n->MustTake(2, 1, pb::MsgProp);
      n->Send(prop);

      // index = 2
      n->ReplicateAppend(1);

      for (auto r : n->Peers()) {
        ASSERT_EQ(r->log_->CommitIndex(), 2);

        auto ev = r->log_->AllEntries();
        ASSERT_EQ(ev.size(), 2);
        Entry_ASSERT_EQ(ev[0], PBEntry().Term(1).Index(1).Data("").v);
        Entry_ASSERT_EQ(ev[1], PBEntry().Term(1).Index(2).Data("some data").v);
      }
    }
  }

  static void TestRestore() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2, 3}).v;

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));

    pb::Snapshot tmp;
    tmp.CopyFrom(snap);
    ASSERT_TRUE(r->restore(tmp));

    ASSERT_EQ(r->log_->LastIndex(), snap.metadata().index());
    ASSERT_EQ(r->log_->Term(snap.metadata().index()).GetValue(), snap.metadata().term());

    std::set<uint64_t> actual;
    for (const auto &e : r->prs_) {
      actual.insert(e.first);
    }

    std::set<uint64_t> expected;
    for (const auto &e : snap.metadata().conf_state().nodes()) {
      expected.insert(e);
    }

    ASSERT_EQ(actual, expected);

    tmp.CopyFrom(snap);
    ASSERT_FALSE(r->restore(tmp));
  }

  static void TestRestoreIgnoreSnapshot() {
    EntryVec previousEnts = {PBEntry().Index(1).Term(1).v, PBEntry().Index(2).Term(1).v,
                             PBEntry().Index(3).Term(1).v};
    uint64_t commit = 1;

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->log_->Append(previousEnts);
    r->log_->CommitTo(commit);

    // ignore snapshot
    auto snap = PBSnapshot().MetaIndex(commit).MetaTerm(1).MetaConfState({1, 2}).v;
    ASSERT_FALSE(r->restore(snap));
    ASSERT_EQ(r->log_->CommitIndex(), commit);

    // ignore snapshot and fast forward commit
    snap = PBSnapshot().MetaIndex(commit + 1).MetaTerm(1).MetaConfState({1, 2}).v;
    ASSERT_FALSE(r->restore(snap));
    ASSERT_EQ(r->log_->CommitIndex(), commit + 1);
  }

  static void TestProvideSnap() {
    // restore the state machine from a snapshot so it has a compacted log and a snapshot
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));
    r->restore(snap);

    r->becomeCandidate();
    r->becomeLeader();

    // force set the next of node 2, so that node 2 needs a snapshot
    r->prs_[2].NextIndex(r->log_->FirstIndex());

    r->Step(PBMessage()
                .From(2)
                .To(1)
                .Type(pb::MsgAppResp)
                .Index(r->prs_[2].NextIndex() - 1)
                .Term(1)
                .Reject()
                .v);

    ASSERT_EQ(r->mails_.size(), 1);
    ASSERT_EQ(r->mails_[0].type(), pb::MsgSnap);
  }
};

}  // namespace yaraft

using namespace yaraft;

TEST(Raft, StepIgnoreOldTermMsg) {
  RaftTest::TestStepIgnoreOldTermMsg();
}

TEST(Raft, HandleAppendEntries) {
  RaftTest::TestHandleMsgApp();
}

TEST(Raft, StateTransition) {
  RaftTest::TestStateTransition();
}

TEST(Raft, HandleHeartbeat) {
  RaftTest::TestHandleHeartbeat();
}

TEST(Raft, HandleHeartbeatResp) {
  RaftTest::TestHandleHeartbeatResp();
}

TEST(Raft, LeaderElection) {
  RaftTest::TestLeaderElection(false);
}

TEST(Raft, LeaderElectionPreVote) {
  RaftTest::TestLeaderElection(true);
}

TEST(Raft, LogReplication) {
  RaftTest::TestLogReplication();
}

TEST(Raft, LeaderCycle) {
  RaftTest::TestLeaderCycle(false);
}

TEST(Raft, LeaderCyclePreVote) {
  RaftTest::TestLeaderCycle(true);
}

TEST(Raft, Commit) {
  RaftTest::TestCommit();
}

TEST(Raft, CampaignWhileLeader) {
  RaftTest::TestCampaignWhileLeader();
}

TEST(Raft, DuelingCandidates) {
  RaftTest::TestDuelingCandidates();
}

TEST(Raft, DuelingPreCandidates) {
  RaftTest::TestDuelingPreCandidates();
}

TEST(Raft, VoteFromAnyState) {
  RaftTest::TestVoteFromAnyState(pb::MsgVote);
}

TEST(Raft, PreVoteFromAnyState) {
  RaftTest::TestVoteFromAnyState(pb::MsgPreVote);
}

TEST(Raft, SingleNodeCandidate) {
  RaftTest::TestSingleNodeCandidate();
}

TEST(Raft, SingleNodePreCandidate) {
  RaftTest::TestSingleNodePreCandidate();
}

TEST(Raft, SingleNodeCommit) {
  RaftTest::TestSingleNodeCommit();
}

TEST(Raft, ProposalByProxy) {
  RaftTest::TestProposalByProxy();
}

TEST(Raft, Restore) {
  RaftTest::TestRestore();
}

TEST(Raft, RestoreIgnoreSnapshot) {
  RaftTest::TestRestoreIgnoreSnapshot();
}

TEST(Raft, ProvideSnap) {
  RaftTest::TestProvideSnap();
}
