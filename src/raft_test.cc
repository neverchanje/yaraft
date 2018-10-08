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

#include <yaraft/memory_storage.h>

#include "exception.h"
#include "raft.h"
#include "test_utils.h"

namespace yaraft {

void preVoteConfig(Config *c) {
  c->preVote = true;
}

Raft *entsWithConfig(std::function<void(Config *)> configFunc, std::vector<uint64_t> terms) {
  auto storage = new MemoryStorage();
  for (uint64_t i = 0; i < terms.size(); i++) {
    uint64_t term = terms[i];
    storage->Append(idl::EntryVec{idl::Entry().index(i + 1).term(term)});
  }
  auto cfg = newTestConfig(1, {}, 5, 1, storage);
  if (configFunc) {
    configFunc(cfg);
  }
  auto sm = Raft::New(cfg);
  sm->reset(terms[terms.size() - 1]);
  return sm;
}

// votedWithConfig creates a raft state machine with Vote and Term set
// to the given value but no log entries (indicating that it voted in
// the given term but has not received any logs).
Raft *votedWithConfig(std::function<void(Config *)> configFunc, uint64_t vote, uint64_t term) {
  auto storage = new MemoryStorage();
  storage->SetHardState(idl::HardState().vote(vote).term(term));
  auto cfg = newTestConfig(1, {}, 5, 1, storage);
  if (configFunc) {
    configFunc(cfg);
  }
  auto sm = Raft::New(cfg);
  sm->reset(term);
  return sm;
}

class RaftTest : public BaseTest {
 public:
  static void testLeaderElection(bool preVote) {
    std::function<void(Config *)> cfg;
    StateType candState = StateType::kCandidate;
    uint64_t candTerm = 1;
    if (preVote) {
      cfg = preVoteConfig;
      // In pre-vote mode, an election that fails to complete
      // leaves the node in pre-candidate state without advancing
      // the term.
      candState = StateType::kPreCandidate;
      candTerm = 0;
    }

    struct TestData {
      NetworkSPtr network;
      StateType state;
      uint64_t expTerm;
    } tests[] = {
        {Network(cfg, {nullptr, nullptr, nullptr}).SPtr(), StateType ::kLeader, 1},
        {Network(cfg, {nullptr, nullptr, nopStepper}).SPtr(), StateType ::kLeader, 1},
        {Network(cfg, {nullptr, nopStepper, nopStepper}).SPtr(), candState, candTerm},
        {Network(cfg, {nullptr, nopStepper, nopStepper, nullptr}).SPtr(), candState, candTerm},
        {Network(cfg, {nullptr, nopStepper, nopStepper, nullptr, nullptr}).SPtr(), StateType ::kLeader, 1},

        // three logs further along than 0, but in the same term so rejections
        // are returned instead of the votes being ignored.
        {Network(cfg, {nullptr, entsWithConfig(cfg, {1}), entsWithConfig(cfg, {1}), entsWithConfig(cfg, {1, 1}), nullptr}).SPtr(),
         StateType::kFollower, 1},
    };

    for (auto tt : tests) {
      tt.network->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
      auto sm = static_cast<Raft *>(tt.network->peers_[1]);
      ASSERT_EQ(sm->state_, tt.state);
      ASSERT_EQ(sm->term_, tt.expTerm);
    }
  }

  // TestLearnerElectionTimeout verfies that the leader should not start election even
  // when times out.
  static void TestLearnerElectionTimeout() {
    RaftUPtr n1(newTestLearnerRaft(1, {1}, {2}, 10, 1, new MemoryStorage()));
    RaftUPtr n2(newTestLearnerRaft(2, {1}, {2}, 10, 1, new MemoryStorage()));

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);

    // n2 is learner. Learner should not start election even when times out.
    setRandomizedElectionTimeout(n2.get(), n2->electionTimeout_);
    for (int i = 0; i < n2->electionTimeout_; i++) {
      n2->tick();
    }

    ASSERT_EQ(n2->state_, StateType::kFollower);
  }

  // TestLearnerPromotion verifies that the learner should not election until
  // it is promoted to a normal peer.
  static void TestLearnerPromotion() {
    auto n1 = newTestLearnerRaft(1, {1}, {2}, 10, 1, new MemoryStorage());
    auto n2 = newTestLearnerRaft(2, {1}, {2}, 10, 1, new MemoryStorage());

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);

    NetworkSPtr nt(new Network({n1, n2}));

    ASSERT_NE(n1->state_, StateType::kLeader);

    // n1 should become leader
    setRandomizedElectionTimeout(n1, n1->electionTimeout_);
    for (int i = 0; i < n1->electionTimeout_; i++) {
      n1->tick();
    }

    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgBeat)});

    n1->addNode(2);
    n2->addNode(2);
    ASSERT_FALSE(n2->isLearner_);

    // n2 start election, should become leader
    setRandomizedElectionTimeout(n2, n2->electionTimeout_);
    for (int i = 0; i < n2->electionTimeout_; i++) {
      n2->tick();
    }

    nt->Send({idl::Message().from(2).to(2).type(idl::MsgBeat)});
    ASSERT_EQ(n1->state_, StateType::kFollower);
    ASSERT_EQ(n2->state_, StateType::kLeader);
  }

  // TestLearnerCannotVote checks that a learner can't vote even it receives a valid Vote request.
  static void TestLearnerCannotVote() {
    RaftUPtr n2(newTestLearnerRaft(2, {1}, {2}, 10, 1, new MemoryStorage()));

    n2->becomeFollower(1, kNone);

    n2->Step(idl::Message().from(1).to(2).term(2).type(idl::MsgVote).logterm(11).index(11));

    ASSERT_EQ(n2->msgs_.size(), 0);
  }

  // testLeaderCycle verifies that each node in a cluster can campaign
  // and be elected in turn. This ensures that elections (including
  // pre-vote) work when not starting from a clean slate (as they do in
  // TestLeaderElection)
  static void testLeaderCycle(bool preVote) {
    std::function<void(Config *)> cfg;
    if (preVote) {
      cfg = preVoteConfig;
    }

    NetworkSPtr n(new Network(cfg, {nullptr, nullptr, nullptr}));
    for (uint64_t campaignerID = 1; campaignerID <= 3; campaignerID++) {
      n->Send({idl::Message().from(campaignerID).to(campaignerID).type(idl::MsgHup)});

      for (auto kv : n->peers_) {
        auto sm = static_cast<Raft *>(kv.second);
        if (sm->id_ == campaignerID && sm->state_ != StateType::kLeader) {
          ASSERT_TRUE(false);
        } else if (sm->id_ != campaignerID && sm->state_ != StateType::kFollower) {
          ASSERT_TRUE(false);
        }
      }
    }
  }

  static void testLeaderElectionOverwriteNewerLogs(bool preVote) {
    std::function<void(Config *)> cfg;
    if (preVote) {
      cfg = preVoteConfig;
    }
    // This network represents the results of the following sequence of
    // events:
    // - Node 1 won the election in term 1.
    // - Node 1 replicated a log entry to node 2 but died before sending
    //   it to other nodes.
    // - Node 3 won the second election in term 2.
    // - Node 3 wrote an entry to its logs but died without sending it
    //   to any other nodes.
    //
    // At this point, nodes 1, 2, and 3 all have uncommitted entries in
    // their logs and could win an election at term 3. The winner's log
    // entry overwrites the losers'. (TestLeaderSyncFollowerLog tests
    // the case where older log entries are overwritten, so this test
    // focuses on the case where the newer entries are lost).
    NetworkSPtr n(new Network(cfg,
                              {entsWithConfig(cfg, {1}),       // Node 1: Won first election
                               entsWithConfig(cfg, {1}),       // Node 2: Got logs from node 1
                               entsWithConfig(cfg, {2}),       // Node 3: Won second election
                               votedWithConfig(cfg, 3, 2),     // Node 4: Voted but didn't get logs
                               votedWithConfig(cfg, 3, 2)}));  // Node 5: Voted but didn't get logs

    // Node 1 campaigns. The election fails because a quorum of nodes
    // know about the election that already happened at term 2. Node 1's
    // term is pushed ahead to 2.
    n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto sm1 = static_cast<Raft *>(n->peers_[1]);
    ASSERT_EQ(sm1->state_, StateType::kFollower);
    ASSERT_EQ(sm1->term_, 2);

    // Node 1 campaigns again with a higher term. This time it succeeds.
    n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    ASSERT_EQ(sm1->state_, StateType::kLeader);
    ASSERT_EQ(sm1->term_, 3);

    // Now all nodes agree on a log entry with term 1 at index 1 (and
    // term 3 at index 2).
    for (auto kv : n->peers_) {
      uint64_t i = kv.first;
      auto sm = static_cast<Raft *>(n->peers_[i]);

      auto entries = sm->raftLog_->allEntries();
      ASSERT_EQ(entries.size(), 2);
      ASSERT_EQ(entries[0].term(), 1);
      ASSERT_EQ(entries[1].term(), 3);
    }
  }

  static void testVoteFromAnyState(idl::MessageType vt) {
    for (int i = 0; i < int(StateType::kStateNum); i++) {
      auto st = StateType(i);
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));

      if (st == StateType::kFollower) {
        r->becomeFollower(r->term_, 3);
      } else if (st == StateType::kPreCandidate) {
        r->becomePreCandidate();
      } else if (st == StateType::kCandidate) {
        r->becomeCandidate();
      } else if (st == StateType::kLeader) {
        r->becomeCandidate();
        r->becomeLeader();
      }

      // Note that setting our state above may have advanced r.Term
      // past its initial value.
      uint64_t origTerm = r->term_;
      uint64_t newTerm = r->term_ + 1;
      auto err = r->Step(idl::Message()
                             .from(2)
                             .to(1)
                             .type(vt)
                             .term(newTerm)
                             .logterm(newTerm)
                             .index(42));
      ASSERT_TRUE(err.is_ok());
      ASSERT_EQ(r->msgs_.size(), 1);

      auto resp = r->msgs_[0];
      ASSERT_EQ(resp.type(), voteRespMsgType(vt));
      ASSERT_EQ(resp.reject(), false);

      // If this was a real vote, we reset our state and term.
      if (vt == idl::MsgVote) {
        ASSERT_EQ(r->state_, StateType::kFollower);
        ASSERT_EQ(r->term_, newTerm);
        ASSERT_EQ(r->vote_, 2);
      } else {
        // In a prevote, nothing changes.
        ASSERT_EQ(r->state_, st);
        ASSERT_EQ(r->term_, origTerm);

        // if st == StateType::kFollower or StateType::kPreCandidate, r hasn't voted yet.
        // In StateType::kCandidate or StateType::kLeader, it's voted for itself.
        ASSERT_FALSE(r->vote_ != kNone && r->vote_ != 1);
      }
    }
  }

  static void TestLogReplication() {
    struct TestData {
      NetworkSPtr n;
      std::vector<idl::Message> msgs;
      uint64_t wcommitted;
    } tests[] = {
        {
            Network({nullptr, nullptr, nullptr}).SPtr(),
            {
                idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
            },
            2,
        },
        {
            Network({nullptr, nullptr, nullptr}).SPtr(),
            {
                idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
                idl::Message().from(1).to(2).type(idl::MsgHup),
                idl::Message().from(1).to(2).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
            },
            4,
        },
    };

    for (auto tt : tests) {
      tt.n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

      std::vector<idl::Message> tt_msgs;
      for (auto m : tt.msgs) {
        tt_msgs.push_back(m.DeepClone());
      }
      for (auto m : tt.msgs) {
        tt.n->Send({m});
      }

      for (auto kv : tt.n->peers_) {
        uint64_t j = kv.first;
        auto sm = static_cast<Raft *>(kv.second);
        ASSERT_EQ(sm->raftLog_->committed_, tt.wcommitted);

        idl::EntryVec ents;
        for (auto e : nextEnts(sm, tt.n->storage_[j])) {
          if (!e.data().empty()) {
            ents.Append(idl::EntryVec{e});
          }
        }
        std::vector<idl::Message> props;
        for (auto m : tt_msgs) {
          if (m.type() == idl::MsgProp) {
            props.push_back(m);
          }
        }
        for (int k = 0; k < props.size(); k++) {
          ASSERT_EQ(ents[k].data(), props[k].entries()[0].data());
        }
      }
    }
  }

  // TestLearnerLogReplication tests that a learner can receive entries from the leader.
  static void TestLearnerLogReplication() {
    auto n1 = newTestLearnerRaft(1, {1}, {2}, 10, 1, new MemoryStorage);
    auto n2 = newTestLearnerRaft(2, {1}, {2}, 10, 1, new MemoryStorage);

    NetworkSPtr nt(new Network({n1, n2}));

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);

    setRandomizedElectionTimeout(n1, n1->electionTimeout_);
    for (int i = 0; i < n1->electionTimeout_; i++) {
      n1->tick();
    }

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgBeat)});

    // n1 is leader and n2 is learner
    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->isLearner_, true);

    uint64_t nextCommitted = n1->raftLog_->committed_ + 1;
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    ASSERT_EQ(n1->raftLog_->committed_, nextCommitted);
    ASSERT_EQ(n1->raftLog_->committed_, n2->raftLog_->committed_);
    ASSERT_EQ(n1->getProgress(2)->match_, n2->raftLog_->committed_);
  }

  static void TestSingleNodeCommit() {
    NetworkSPtr tt(new Network({nullptr}));
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    tt->Send({idl::Message().from(2).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    tt->Send({idl::Message().from(2).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});

    auto sm = static_cast<Raft *>(tt->peers_[1]);
    ASSERT_EQ(sm->raftLog_->committed_, 3);
  }

  // TestCannotCommitWithoutNewTermEntry tests the entries cannot be committed
  // when leader changes, no new proposal comes in and ChangeTerm proposal is
  // filtered.
  void TestCannotCommitWithoutNewTermEntry() {
    NetworkSPtr tt(new Network({nullptr, nullptr, nullptr, nullptr, nullptr}));
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // 0 cannot reach 2,3,4
    tt->Cut(1, 3);
    tt->Cut(1, 4);
    tt->Cut(1, 5);

    tt->Send({
        idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
    });
    tt->Send({
        idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
    });

    auto sm = static_cast<Raft *>(tt->peers_[1]);
    ASSERT_EQ(sm->raftLog_->committed_, 1);

    // network recovery
    tt->Recover();
    // avoid committing ChangeTerm proposal
    tt->Ignore(idl::MsgApp);

    // elect 2 as the new leader with term 2
    tt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});

    // no log entries from previous term should be committed
    sm = static_cast<Raft *>(tt->peers_[2]);
    ASSERT_EQ(sm->raftLog_->committed_, 1);

    tt->Recover();
    // send heartbeat; reset wait
    tt->Send({idl::Message().from(2).to(2).type(idl::MsgBeat)});
    // append an entry at current term
    tt->Send({
        idl::Message().from(2).to(2).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
    });
    // expect the committed to be advanced
    ASSERT_EQ(sm->raftLog_->committed_, 5);
  }

  // TestCommitWithoutNewTermEntry tests the entries could be committed
  // when leader changes, no new proposal comes in.
  void TestCommitWithoutNewTermEntry() {
    NetworkSPtr tt(new Network({nullptr, nullptr, nullptr, nullptr, nullptr}));
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // 0 cannot reach 2,3,4
    tt->Cut(1, 3);
    tt->Cut(1, 4);
    tt->Cut(1, 5);

    tt->Send({
        idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
    });
    tt->Send({
        idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}),
    });

    auto sm = static_cast<Raft *>(tt->peers_[1]);
    ASSERT_EQ(sm->raftLog_->committed_, 1);

    // network recovery
    tt->Recover();

    // elect 1 as the new leader with term 2
    // after append a ChangeTerm entry from the current term, all entries
    // should be committed
    tt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});

    ASSERT_EQ(sm->raftLog_->committed_, 4);
  }

  static void TestDuelingCandidates() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    NetworkSPtr n(new Network({a, b, c}));
    n->Cut(1, 3);
    n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    n->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // 1 becomes leader since it receives votes from 1 and 2
    auto sm = static_cast<Raft *>(n->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);

    // 3 stays as candidate since it receives a vote from 3 and a rejection from 2
    sm = static_cast<Raft *>(n->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kCandidate);

    n->Recover();

    // candidate 3 now increases its term and tries to vote again
    // we expect it to disrupt the leader 1 since it has a higher term
    // 3 will be follower again since both 1 and 2 rejects its vote request since 3 does not have a long enough log
    n->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    auto s = new MemoryStorage;
    s->ents_ = {{}, idl::Entry().index(1).term(1).data("")};
    auto wlog = std::make_shared<RaftLog>(s);
    wlog->committed_ = 1;
    wlog->unstable_.offset = 2;

    struct TestData {
      Raft *sm;
      StateType state;
      uint64_t term;
      RaftLogSPtr raftLog;
    } tests[] = {
        {a, StateType::kFollower, 2, wlog},
        {b, StateType::kFollower, 2, wlog},
        {c, StateType::kFollower, 2, std::make_shared<RaftLog>(new MemoryStorage())},
    };

    uint64_t i = 0;
    for (auto tt : tests) {
      ASSERT_EQ(tt.sm->state_, tt.state);
      ASSERT_EQ(tt.sm->term_, tt.term);

      auto base = tt.raftLog->ltoa();
      sm = static_cast<Raft *>(n->peers_[i + 1]);
      ASSERT_EQ(sm->raftLog_->ltoa(), base);

      i++;
    }
  }

  static void TestDuelingPreCandidates() {
    auto cfgA = newTestConfig(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto cfgB = newTestConfig(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto cfgC = newTestConfig(3, {1, 2, 3}, 10, 1, new MemoryStorage());
    cfgA->preVote = true;
    cfgB->preVote = true;
    cfgC->preVote = true;
    auto a = Raft::New(cfgA);
    auto b = Raft::New(cfgB);
    auto c = Raft::New(cfgC);

    NetworkSPtr n(new Network({a, b, c}));
    n->Cut(1, 3);

    n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    n->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // 1 becomes leader since it receives votes from 1 and 2
    auto sm = static_cast<Raft *>(n->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);

    // 3 campaigns then reverts to follower when its PreVote is rejected
    sm = static_cast<Raft *>(n->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kFollower);

    n->Recover();

    // Candidate 3 now increases its term and tries to vote again.
    // With PreVote, it does not disrupt the leader.
    n->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    auto s = new MemoryStorage;
    s->ents_ = {{}, idl::Entry().index(1).term(1).data("")};
    auto wlog = std::make_shared<RaftLog>(s);
    wlog->committed_ = 1;
    wlog->unstable_.offset = 2;

    struct TestData {
      Raft *sm;
      StateType state;
      uint64_t term;
      RaftLogSPtr raftLog;
    } tests[] = {
        {a, StateType::kLeader, 1, wlog},
        {b, StateType::kFollower, 1, wlog},
        {c, StateType::kFollower, 1, std::make_shared<RaftLog>(new MemoryStorage())},
    };

    uint64_t i = 0;
    for (auto tt : tests) {
      ASSERT_EQ(tt.sm->state_, tt.state);
      ASSERT_EQ(tt.sm->term_, tt.term);

      auto base = tt.raftLog->ltoa();
      sm = static_cast<Raft *>(n->peers_[i + 1]);
      ASSERT_EQ(sm->raftLog_->ltoa(), base);

      i++;
    }
  }

  static void TestCandidateConcede() {
    NetworkSPtr n(new Network({nullptr, nullptr, nullptr}));
    n->Isolate(1);

    n->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    n->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // heal the partition
    n->Recover();
    // send heartbeat; reset wait
    n->Send({idl::Message().from(3).to(3).type(idl::MsgBeat)});

    std::string data("force follower");
    // send a proposal to 3 to flush out a MsgApp to 1
    n->Send({idl::Message().from(3).to(3).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    // send heartbeat; flush out commit
    n->Send({idl::Message().from(3).to(3).type(idl::MsgBeat)});

    auto a = static_cast<Raft *>(n->peers_[1]);
    ASSERT_EQ(a->state_, StateType::kFollower);
    ASSERT_EQ(a->term_, 1);

    auto s = new MemoryStorage;
    s->ents_ = {{}, idl::Entry().index(1).term(1).data(""), idl::Entry().index(2).term(1).data(data)};
    RaftLog wantLog(s);
    wantLog.unstable_.offset = 3;
    wantLog.committed_ = 2;

    for (auto kv : n->peers_) {
      auto sm = static_cast<Raft *>(kv.second);
      ASSERT_EQ(sm->raftLog_->ltoa(), wantLog.ltoa());
    }
  }

  static void TestSingleNodeCandidate() {
    NetworkSPtr tt(new Network({nullptr}));
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto sm = static_cast<Raft *>(tt->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);
  }

  static void TestSingleNodePreCandidate() {
    NetworkSPtr tt(new Network(preVoteConfig, {nullptr}));
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto sm = static_cast<Raft *>(tt->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);
  }

  void TestProposal() {
    struct TestData {
      NetworkSPtr network;
      bool success;
    } tests[] = {
        {Network({nullptr, nullptr, nullptr}).SPtr(), true},
        {Network({nullptr, nullptr, nopStepper}).SPtr(), true},
        {Network({nullptr, nopStepper, nopStepper}).SPtr(), false},
        {Network({nullptr, nopStepper, nopStepper, nullptr}).SPtr(), false},
        {Network({nullptr, nopStepper, nopStepper, nullptr, nullptr}).SPtr(), true},
    };

    for (auto tt : tests) {
      auto send = [&](idl::Message &m) {
        try {
          tt.network->Send({m});
        } catch (RaftError &e) {
          // only recover is we expect it to panic so
          // panics we don't expect go up.
          if (tt.success) {
            ASSERT_TRUE(false);
          }
        }
      };
      std::string data("somedata");

      // promote 0 the leader
      send(idl::Message().from(1).to(1).type(idl::MsgHup));
      send(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data(data)}));

      std::unique_ptr<RaftLog> wantLog(new RaftLog(new MemoryStorage));
      if (tt.success) {
        auto s = new MemoryStorage;
        s->ents_ = {{}, idl::Entry().index(1).term(1).data(""), idl::Entry().index(2).term(1).data(data)};
        wantLog.reset(new RaftLog(s));
        wantLog->unstable_.offset = 3;
        wantLog->committed_ = 2;
      }
      auto base = wantLog->ltoa();
      for (auto kv : tt.network->peers_) {
        if (kv.second->type() == "raft") {
          auto sm = static_cast<Raft *>(kv.second);
          ASSERT_EQ(sm->raftLog_->ltoa(), wantLog->ltoa());
        }
      }
      auto sm = static_cast<Raft *>(tt.network->peers_[1]);
      ASSERT_EQ(sm->term_, 1);
    }
  }

  void TestProposalByProxy() {
    std::string data("somedata");
    struct TestData {
      NetworkSPtr network;
    } tests[] = {
        {Network({nullptr, nullptr, nullptr}).SPtr()},
        {Network({nullptr, nullptr, nopStepper}).SPtr()},
    };

    for (auto tt : tests) {
      // promote 0 the leader
      tt.network->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

      // propose via follower
      tt.network->Send({idl::Message().from(2).to(2).type(idl::MsgProp).entries({idl::Entry().data(data)})});

      auto s = new MemoryStorage;
      s->ents_ = {{}, idl::Entry().index(1).term(1).data(""), idl::Entry().index(2).term(1).data(data)};
      RaftLog wantLog(s);
      wantLog.unstable_.offset = 3;
      wantLog.committed_ = 2;

      auto base = wantLog.ltoa();
      for (auto kv : tt.network->peers_) {
        if (kv.second->type() == "raft") {
          auto sm = static_cast<Raft *>(kv.second);
          ASSERT_EQ(sm->raftLog_->ltoa(), wantLog.ltoa());
        }
      }
      auto sm = static_cast<Raft *>(tt.network->peers_[1]);
      ASSERT_EQ(sm->term_, 1);
    }
  }

  static void TestCommit() {
    struct TestData {
      std::vector<uint64_t> matches;
      idl::EntryVec logs;
      uint64_t smTerm;

      uint64_t w;
    } tests[] = {
        /// single
        {{1}, {newEntry(1, 1)}, 1, 1},
        {{1}, {newEntry(1, 1)}, 2, 0},
        {{2}, {newEntry(1, 1), newEntry(2, 2)}, 2, 2},
        {{1}, {newEntry(1, 2)}, 2, 1},

        // odd
        {{2, 1, 1}, {newEntry(1, 1), newEntry(2, 2)}, 1, 1},
        {{2, 1, 1}, {newEntry(1, 1), newEntry(2, 1)}, 2, 0},
        {{2, 1, 2}, {newEntry(1, 1), newEntry(2, 2)}, 2, 2},
        {{2, 1, 2}, {newEntry(1, 1), newEntry(2, 1)}, 2, 0},

        // even
        {{2, 1, 1, 1}, {newEntry(1, 1), newEntry(2, 2)}, 1, 1},
        {{2, 1, 1, 1}, {newEntry(1, 1), newEntry(2, 1)}, 2, 0},
        {{2, 1, 1, 2}, {newEntry(1, 1), newEntry(2, 2)}, 1, 1},
        {{2, 1, 1, 2}, {newEntry(1, 1), newEntry(2, 1)}, 2, 0},
        {{2, 1, 2, 2}, {newEntry(1, 1), newEntry(2, 2)}, 2, 2},
        {{2, 1, 2, 2}, {newEntry(1, 1), newEntry(2, 1)}, 2, 0},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(t.logs);
      storage->hardState_ = idl::HardState().term(t.smTerm);

      RaftUPtr r(newTestRaft(1, {1}, 5, 1, storage));
      for (uint64_t i = 0; i < t.matches.size(); i++) {
        r->setProgress(i + 1, t.matches[i], t.matches[i] + 1, false);
      }
      r->maybeCommit();
      ASSERT_EQ(r->raftLog_->committed_, t.w);
    }
  }

  void TestPastElectionTimeout() {
    struct TestData {
      int elapse;
      double wprobability;
      bool round;
    } tests[] = {
        {5, 0, false},
        {10, 0.1, true},
        {13, 0.4, true},
        {15, 0.6, true},
        {18, 0.9, true},
        {20, 1, false},
    };

    for (auto tt : tests) {
      RaftUPtr sm(newTestRaft(1, {1}, 10, 1, new MemoryStorage));
      sm->electionElapsed_ = tt.elapse;
      int c = 0;
      for (int j = 0; j < 10000; j++) {
        sm->resetRandomizedElectionTimeout();
        if (sm->pastElectionTimeout()) {
          c++;
        }
      }
      double got = double(c) / 10000.0;
      if (tt.round) {
        got = std::floor(got * 10 + 0.5) / 10.0;
      }
      ASSERT_EQ(got, tt.wprobability);
    }
  }

  // ensure that the Step function ignores the message from old term and does not pass it to the
  // actual stepX function.
  static void TestStepIgnoreOldTermMsg() {
    bool called = false;
    RaftUPtr sm(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));

    auto fakeStep = [&](idl::Message &m) -> error_s {
      called = true;
      return error_s::ok();
    };
    sm->step_ = fakeStep;
    sm->term_ = 2;
    sm->Step(idl::Message().type(idl::MsgApp).term(sm->term_ - 1));
    ASSERT_FALSE(called);
  }

  // TestHandleMsgApp ensures:
  // 1. Reply false if log doesn’t contain an entry at prevLogIndex whose term matches prevLogTerm.
  // 2. If an existing entry conflicts with a new one (same index but different terms),
  //    delete the existing entry and all that follow it; append any new entries not already in the log.
  // 3. If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry).
  static void TestHandleMsgApp() {
    struct TestData {
      idl::Message m;
      uint64_t wIndex;
      uint64_t wCommit;
      bool wReject;
    } tests[] = {
        // Ensure 1
        // previous log mismatch
        {idl::Message().type(idl::MsgApp).term(2).logterm(3).index(2).commit(3), 2, 0, true},
        // previous log non-exist
        {idl::Message().type(idl::MsgApp).term(2).logterm(3).index(3).commit(3), 2, 0, true},

        // Ensure 2
        {idl::Message().type(idl::MsgApp).term(2).logterm(1).index(1).commit(1), 2, 1, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(0).index(0).commit(1).entries({newEntry(1, 2)}), 1, 1, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(2).index(2).commit(3).entries({newEntry(3, 2), newEntry(4, 2)}), 4, 3, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(2).index(2).commit(4).entries({newEntry(3, 2)}), 3, 3, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(1).index(1).commit(4).entries({newEntry(2, 2)}), 2, 2, false},

        // Ensure 3
        // match entry 1, commit up to last new entry 1
        {idl::Message().type(idl::MsgApp).term(1).logterm(1).index(1).commit(3), 2, 1, false},
        {idl::Message().type(idl::MsgApp).term(1).logterm(1).index(1).commit(3).entries({newEntry(2, 2)}), 2, 2, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(2).index(2).commit(3), 2, 2, false},
        {idl::Message().type(idl::MsgApp).term(2).logterm(2).index(2).commit(4), 2, 2, false},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(idl::EntryVec({newEntry(1, 1), newEntry(2, 2)}));
      RaftUPtr sm(newTestRaft(1, {1}, 10, 1, storage));
      sm->becomeFollower(2, 0);

      sm->handleAppendEntries(t.m);
      ASSERT_EQ(sm->raftLog_->lastIndex(), t.wIndex);
      ASSERT_EQ(sm->raftLog_->committed_, t.wCommit);
      auto ms = sm->readMessages();
      ASSERT_EQ(ms.size(), 1);
      ASSERT_EQ(ms[0].reject(), t.wReject);
    }
  }

  static void TestHandleHeartbeat() {
    uint64_t commit = 2;
    struct TestData {
      idl::Message m;
      uint64_t wCommit;
    } tests[] = {
        {idl::Message().from(2).to(1).type(idl::MsgHeartbeat).term(2).commit(commit + 1), commit + 1},

        // do not decrease commit
        {idl::Message().from(2).to(1).type(idl::MsgHeartbeat).term(2).commit(commit - 1), commit},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(idl::EntryVec{newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)});
      RaftUPtr sm(newTestRaft(1, {1, 2}, 10, 1, storage));
      sm->becomeFollower(2, 0);
      sm->raftLog_->commitTo(commit);
      sm->handleHeartbeat(t.m);
      ASSERT_EQ(sm->raftLog_->committed_, t.wCommit);

      auto ms = sm->readMessages();
      ASSERT_EQ(ms.size(), 1);
      ASSERT_EQ(ms[0].type(), idl::MsgHeartbeatResp);
    }
  }

  // TestHandleHeartbeatResp ensures that we re-send log entries when we get a
  // heartbeat response.
  static void TestHandleHeartbeatResp() {
    auto storage = new MemoryStorage();
    storage->Append(idl::EntryVec{newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)});
    RaftUPtr sm(newTestRaft(1, {1, 2}, 5, 1, storage));
    sm->becomeCandidate();
    sm->becomeLeader();
    sm->raftLog_->commitTo(sm->raftLog_->lastIndex());

    // A heartbeat response from a node that is behind; re-send MsgApp
    sm->Step(idl::Message().from(2).type(idl::MsgHeartbeatResp));
    auto msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgApp);

    // A second heartbeat response generates another MsgApp re-send
    sm->Step(idl::Message().from(2).type(idl::MsgHeartbeatResp));
    msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgApp);

    // Once we have an MsgAppResp, heartbeats no longer send MsgApp.
    sm->Step(idl::Message().from(2).type(idl::MsgAppResp).index(msgs[0].index() + msgs[0].entries().size()));
    sm->readMessages();

    sm->Step(idl::Message().from(2).type(idl::MsgHeartbeatResp));
    msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 0);
  }

  // TestRaftFreesReadOnlyMem ensures raft will free read request from
  // readOnly readIndexQueue and pendingReadIndex map.
  // related issue: https://go.etcd.io/etcd/issues/7571
  static void TestRaftFreesReadOnlyMem() {
    RaftUPtr sm(newTestRaft(1, {1, 2}, 5, 1, new MemoryStorage));
    sm->becomeCandidate();
    sm->becomeLeader();
    sm->raftLog_->commitTo(sm->raftLog_->lastIndex());

    std::string ctx = ("ctx");

    // leader starts linearizable read request.
    // more info: raft dissertation 6.4, step 2.
    sm->Step(idl::Message().from(2).type(idl::MsgReadIndex).entries({idl::Entry().data(ctx)}));
    auto msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgHeartbeat);
    ASSERT_EQ(msgs[0].context(), ctx);
    ASSERT_EQ(sm->readOnly_->readIndexQueue.size(), 1);
    ASSERT_EQ(sm->readOnly_->pendingReadIndex.size(), 1);
    ASSERT_TRUE(sm->readOnly_->pendingReadIndex.find(ctx) != sm->readOnly_->pendingReadIndex.end());

    // heartbeat responses from majority of followers (1 in this case)
    // acknowledge the authority of the leader.
    // more info: raft dissertation 6.4, step 3.
    sm->Step(idl::Message().from(2).type(idl::MsgHeartbeatResp).context(ctx));
    ASSERT_EQ(sm->readOnly_->readIndexQueue.size(), 0);
    ASSERT_EQ(sm->readOnly_->pendingReadIndex.size(), 0);
    ASSERT_TRUE(sm->readOnly_->pendingReadIndex.find(ctx) == sm->readOnly_->pendingReadIndex.end());
  }

  // TestMsgAppRespWaitReset verifies the resume behavior of a leader
  // MsgAppResp.
  void TestMsgAppRespWaitReset() {
    RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 5, 1, new MemoryStorage));
    sm->becomeCandidate();
    sm->becomeLeader();

    // The new leader has just emitted a new Term 4 entry; consume those messages
    // from the outgoing queue.
    sm->bcastAppend();
    sm->readMessages();

    // Node 2 acks the first entry, making it committed.
    sm->Step(idl::Message().from(2).type(idl::MsgAppResp).index(1));

    ASSERT_EQ(sm->raftLog_->committed_, 1);
    // Also consume the MsgApp messages that update Commit on the followers.
    sm->readMessages();

    // A new command is now proposed on node 1.
    sm->Step(idl::Message().from(1).type(idl::MsgProp).entries({idl::Entry()}));

    // The command is broadcast to all nodes not in the wait state.
    // Node 2 left the wait state due to its MsgAppResp, but node 3 is still waiting.
    auto msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgApp);
    ASSERT_EQ(msgs[0].entries().size(), 1);
    ASSERT_EQ(msgs[0].entries()[0].index(), 2);

    // Now Node 3 acks the first entry. This releases the wait and entry 2 is sent.
    sm->Step(idl::Message().from(3).type(idl::MsgAppResp).index(1));
    msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgApp);
    ASSERT_EQ(msgs[0].to(), 3);
    ASSERT_EQ(msgs[0].entries().size(), 1);
    ASSERT_EQ(msgs[0].entries()[0].index(), 2);
  }

  static void testRecvMsgVote(idl::MessageType msgType) {
    struct TestData {
      StateType state;
      uint64_t index, logTerm;
      uint64_t voteFor;
      bool wreject;
    } tests[] = {
        {StateType::kFollower, 0, 0, kNone, true},
        {StateType::kFollower, 0, 1, kNone, true},
        {StateType::kFollower, 0, 2, kNone, true},
        {StateType::kFollower, 0, 3, kNone, false},

        {StateType::kFollower, 1, 0, kNone, true},
        {StateType::kFollower, 1, 1, kNone, true},
        {StateType::kFollower, 1, 2, kNone, true},
        {StateType::kFollower, 1, 3, kNone, false},

        {StateType::kFollower, 2, 0, kNone, true},
        {StateType::kFollower, 2, 1, kNone, true},
        {StateType::kFollower, 2, 2, kNone, false},
        {StateType::kFollower, 2, 3, kNone, false},

        {StateType::kFollower, 3, 0, kNone, true},
        {StateType::kFollower, 3, 1, kNone, true},
        {StateType::kFollower, 3, 2, kNone, false},
        {StateType::kFollower, 3, 3, kNone, false},

        {StateType::kFollower, 3, 2, 2, false},
        {StateType::kFollower, 3, 2, 1, true},

        {StateType::kLeader, 3, 3, 1, true},
        {StateType::kPreCandidate, 3, 3, 1, true},
        {StateType::kCandidate, 3, 3, 1, true},
    };

    for (auto tt : tests) {
      RaftUPtr sm(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));
      sm->state_ = tt.state;

      switch (tt.state) {
        case StateType::kFollower:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepFollower(m); };
          break;
        case StateType::kCandidate:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepCandidate(m); };
          break;
        case StateType::kLeader:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepLeader(m); };
          break;
        default:
          break;
      }
      sm->vote_ = tt.voteFor;

      auto storage = new MemoryStorage();
      storage->ents_ = (idl::EntryVec{{}, newEntry(1, 1), newEntry(2, 2)});
      sm->raftLog_.reset(new RaftLog(storage));
      sm->raftLog_->unstable_.offset = 3;

      // raft.Term is greater than or equal to raft.raftLog.lastTerm. In this
      // test we're only testing MsgVote responses when the campaigning node
      // has a different raft log compared to the recipient node.
      // Additionally we're verifying behaviour when the recipient node has
      // already given out its vote for its current term. We're not testing
      // what the recipient node does when receiving a message with a
      // different term number, so we simply initialize both term numbers to
      // be the same.
      uint64_t term = std::max(sm->raftLog_->lastTerm(), tt.logTerm);
      sm->term_ = term;
      sm->Step(idl::Message().type(msgType).term(term).from(2).index(tt.index).logterm(tt.logTerm));

      auto msgs = sm->readMessages();
      ASSERT_EQ(msgs.size(), 1);
      ASSERT_EQ(msgs[0].type(), voteRespMsgType(msgType));
      ASSERT_EQ(msgs[0].reject(), tt.wreject);
    }
  }

  static void TestStateTransition() {
    struct TestData {
      StateType from;
      StateType to;
      bool wallow;
      uint64_t wterm;
      uint64_t wlead;
    } tests[] = {
        {StateType::kFollower, StateType::kFollower, true, 1, kNone},
        {StateType::kFollower, StateType::kPreCandidate, true, 0, kNone},
        {StateType::kFollower, StateType::kCandidate, true, 1, kNone},
        {StateType::kFollower, StateType::kLeader, false, 0, kNone},

        {StateType::kPreCandidate, StateType::kFollower, true, 0, kNone},
        {StateType::kPreCandidate, StateType::kPreCandidate, true, 0, kNone},
        {StateType::kPreCandidate, StateType::kCandidate, true, 1, kNone},
        {StateType::kPreCandidate, StateType::kLeader, true, 0, 1},

        {StateType::kCandidate, StateType::kFollower, true, 0, kNone},
        {StateType::kCandidate, StateType::kPreCandidate, true, 0, kNone},
        {StateType::kCandidate, StateType::kCandidate, true, 1, kNone},
        {StateType::kCandidate, StateType::kLeader, true, 0, 1},

        {StateType::kLeader, StateType::kFollower, true, 1, kNone},
        {StateType::kLeader, StateType::kPreCandidate, false, 0, kNone},
        {StateType::kLeader, StateType::kCandidate, false, 1, kNone},
        {StateType::kLeader, StateType::kLeader, true, 0, 1},
    };

    for (auto t : tests) {
      RaftUPtr raft(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));
      raft->state_ = t.from;

      bool failed = false;
      try {
        switch (t.to) {
          case StateType::kFollower:
            raft->becomeFollower(t.wterm, t.wlead);
            break;
          case StateType::kPreCandidate:
            raft->becomePreCandidate();
            break;
          case StateType::kCandidate:
            raft->becomeCandidate();
            break;
          case StateType::kLeader:
            raft->becomeLeader();
            break;
          default:
            break;
        }

        ASSERT_EQ(raft->term_, t.wterm);
        ASSERT_EQ(raft->lead_, t.wlead);
      } catch (RaftError &e) {
        failed = true;
      }

      ASSERT_EQ(!t.wallow, failed);
    }
  }

  void TestAllServerStepdown() {
    struct TestData {
      StateType state;

      StateType wstate;
      uint64_t wterm;
      uint64_t windex;
    } tests[] = {
        {StateType::kFollower, StateType::kFollower, 3, 0},
        {StateType::kPreCandidate, StateType::kFollower, 3, 0},
        {StateType::kCandidate, StateType::kFollower, 3, 0},
        {StateType::kLeader, StateType::kFollower, 3, 1},
    };

    std::vector<idl::MessageType> tmsgTypes{idl::MsgVote, idl::MsgApp};
    uint64_t tterm = (3);

    for (auto tt : tests) {
      RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
      switch (tt.state) {
        case StateType::kFollower:
          sm->becomeFollower(1, kNone);
          break;
        case StateType::kPreCandidate:
          sm->becomePreCandidate();
          break;
        case StateType::kCandidate:
          sm->becomeCandidate();
          break;
        case StateType::kLeader:
          sm->becomeCandidate();
          sm->becomeLeader();
          break;
        default:
          break;
      }

      for (auto msgType : tmsgTypes) {
        sm->Step(idl::Message().from(2).type(msgType).term(tterm).logterm(tterm));
        ASSERT_EQ(sm->state_, tt.wstate);
        ASSERT_EQ(sm->term_, tt.wterm);
        ASSERT_EQ(sm->raftLog_->lastIndex(), tt.windex);
        ASSERT_EQ(sm->raftLog_->allEntries().size(), tt.windex);
        uint64_t wlead = 2;
        if (msgType == idl::MsgVote) {
          wlead = kNone;
        }
        ASSERT_EQ(sm->lead_, wlead);
      }
    }
  }

  // testCandidateResetTerm tests when a candidate receives a
  // MsgHeartbeat or MsgApp from leader, "Step" resets the term
  // with leader's and reverts back to follower.
  void testCandidateResetTerm(idl::MessageType mt) {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    NetworkSPtr nt(new Network({a, b, c}));

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    ASSERT_EQ(a->state_, StateType::kLeader);
    ASSERT_EQ(b->state_, StateType::kFollower);
    ASSERT_EQ(c->state_, StateType::kFollower);

    // isolate 3 and increase term in rest
    nt->Isolate(3);

    nt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);
    ASSERT_EQ(b->state_, StateType::kFollower);

    c->resetRandomizedElectionTimeout();
    for (uint64_t i = 0; i < c->randomizedElectionTimeout_; i++) {
      c->tick();
    }

    ASSERT_EQ(c->state_, StateType::kCandidate);

    nt->Recover();

    // leader sends to isolated candidate
    // and expects candidate to revert to follower
    nt->Send({idl::Message().from(1).to(3).type(mt)});

    ASSERT_EQ(c->state_, StateType::kFollower);

    // follower c term is reset with leader's
    ASSERT_EQ(a->term_, c->term_);
  }

  static void TestLeaderStepdownWhenQuorumActive() {
    RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 5, 1, new MemoryStorage));

    sm->checkQuorum_ = true;

    sm->becomeCandidate();
    sm->becomeLeader();

    for (uint64_t i = 0; i < sm->electionTimeout_ + 1; i++) {
      sm->Step(idl::Message().from(2).type(idl::MsgHeartbeatResp).term(sm->term_));
      sm->tick();
    }

    ASSERT_EQ(sm->state_, StateType::kLeader);
  }

  static void TestLeaderStepdownWhenQuorumLost() {
    RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 5, 1, new MemoryStorage));

    sm->checkQuorum_ = true;

    sm->becomeCandidate();
    sm->becomeLeader();

    for (uint64_t i = 0; i < sm->electionTimeout_ + 1; i++) {
      sm->tick();
    }

    ASSERT_EQ(sm->state_, StateType::kFollower);
  }

  static void TestLeaderSupersedingWithCheckQuorum() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    a->checkQuorum_ = true;
    b->checkQuorum_ = true;
    c->checkQuorum_ = true;

    NetworkSPtr nt(new Network({a, b, c}));
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 1);

    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);
    ASSERT_EQ(c->state_, StateType::kFollower);

    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // Peer b rejected c's vote since its electionElapsed had not reached to electionTimeout
    ASSERT_EQ(c->state_, StateType::kCandidate);

    // Letting b's electionElapsed reach to electionTimeout
    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(c->state_, StateType::kLeader);
  }

  static void TestLeaderElectionWithCheckQuorum() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    a->checkQuorum_ = true;
    b->checkQuorum_ = true;
    c->checkQuorum_ = true;

    NetworkSPtr nt(new Network({a, b, c}));
    setRandomizedElectionTimeout(a, a->electionTimeout_ + 1);
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 2);

    // Immediately after creation, votes are cast regardless of the
    // election timeout.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);
    ASSERT_EQ(c->state_, StateType::kFollower);

    // need to reset randomizedElectionTimeout larger than electionTimeout again,
    // because the value might be reset to electionTimeout since the last state changes
    setRandomizedElectionTimeout(a, a->electionTimeout_ + 1);
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 2);
    for (uint64_t i = 0; i < a->electionTimeout_; i++) {
      a->tick();
    }
    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kFollower);
    ASSERT_EQ(c->state_, StateType::kLeader);
  }

  // TestFreeStuckCandidateWithCheckQuorum ensures that a candidate with a higher term
  // can disrupt the leader even if the leader still "officially" holds the lease, The
  // leader is expected to step down and adopt the candidate's term
  static void TestFreeStuckCandidateWithCheckQuorum() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    a->checkQuorum_ = true;
    b->checkQuorum_ = true;
    c->checkQuorum_ = true;

    NetworkSPtr nt(new Network({a, b, c}));
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 2);
    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(1);
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(b->state_, StateType::kFollower);
    ASSERT_EQ(c->state_, StateType::kCandidate);
    ASSERT_EQ(c->term_, b->term_ + 1);

    // Vote again for safety
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(b->state_, StateType::kFollower);
    ASSERT_EQ(c->state_, StateType::kCandidate);
    ASSERT_EQ(c->term_, b->term_ + 2);

    nt->Recover();
    nt->Send({idl::Message().from(1).to(3).type(idl::MsgHeartbeat).term(a->term_)});

    // Disrupt the leader so that the stuck peer is freed
    ASSERT_EQ(a->state_, StateType::kFollower);
    ASSERT_EQ(c->term_, a->term_);

    // Vote again, should become leader this time
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(c->state_, StateType::kLeader);
  }

  static void TestNonPromotableVoterWithCheckQuorum() {
    auto a = newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1}, 10, 1, new MemoryStorage());

    a->checkQuorum_ = true;
    b->checkQuorum_ = true;

    NetworkSPtr nt(new Network({a, b}));
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 1);
    // Need to remove 2 again to make it a non-promotable node since newNetwork overwritten some internal states
    b->delProgress(2);

    ASSERT_FALSE(b->promotable());

    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);
    ASSERT_EQ(b->state_, StateType::kFollower);
    ASSERT_EQ(b->lead_, 1);
  }

  // TestDisruptiveFollower tests isolated follower,
  // with slow network incoming from leader, election times out
  // to become a candidate with an increased term. Then, the
  // candiate's response to late leader heartbeat forces the leader
  // to step down.
  static void TestDisruptiveFollower() {
    auto n1 = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n2 = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n3 = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    n1->checkQuorum_ = true;
    n2->checkQuorum_ = true;
    n3->checkQuorum_ = true;

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);
    n3->becomeFollower(1, kNone);

    NetworkSPtr nt(new Network({n1, n2, n3}));

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // check state
    // n1.state == StateLeader
    // n2.state == StateFollower
    // n3.state == StateFollower
    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kFollower);

    // etcd server "advanceTicksForElection" on restart;
    // this is to expedite campaign trigger when given larger
    // election timeouts (e.g. multi-datacenter deploy)
    // Or leader messages are being delayed while ticks elapse
    setRandomizedElectionTimeout(n3, n3->electionTimeout_ + 2);
    for (uint64_t i = 0; i < n3->randomizedElectionTimeout_ - 1; i++) {
      n3->tick();
    }

    // ideally, before last election tick elapses,
    // the follower n3 receives "pb.MsgApp" or "pb.MsgHeartbeat"
    // from leader n1, and then resets its "electionElapsed"
    // however, last tick may elapse before receiving any
    // messages from leader, thus triggering campaign
    n3->tick();

    // n1 is still leader yet
    // while its heartbeat to candidate n3 is being delayed

    // check state
    // n1.state == StateLeader
    // n2.state == StateFollower
    // n3.state == StateCandidate
    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kCandidate);
    // check term
    // n1.Term == 2
    // n2.Term == 2
    // n3.Term == 3
    ASSERT_EQ(n1->term_, 2);
    ASSERT_EQ(n2->term_, 2);
    ASSERT_EQ(n3->term_, 3);

    // while outgoing vote requests are still queued in n3,
    // leader heartbeat finally arrives at candidate n3
    // however, due to delayed network from leader, leader
    // heartbeat was sent with lower term than candidate's
    nt->Send({idl::Message().from(1).to(3).term(n1->term_).type(idl::MsgHeartbeat)});

    // then candidate n3 responds with "pb.MsgAppResp" of higher term
    // and leader steps down from a message with higher term
    // this is to disrupt the current leader, so that candidate
    // with higher term can be freed with following election

    // check state
    // n1.state == StateFollower
    // n2.state == StateFollower
    // n3.state == StateCandidate
    ASSERT_EQ(n1->state_, StateType::kFollower);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kCandidate);
    // check term
    // n1.Term == 3
    // n2.Term == 2
    // n3.Term == 3
    ASSERT_EQ(n1->term_, 3);
    ASSERT_EQ(n2->term_, 2);
    ASSERT_EQ(n3->term_, 3);
  }

  // TestDisruptiveFollowerPreVote tests isolated follower,
  // with slow network incoming from leader, election times out
  // to become a pre-candidate with less log than current leader.
  // Then pre-vote phase prevents this isolated node from forcing
  // current leader to step down, thus less disruptions.
  static void TestDisruptiveFollowerPreVote() {
    auto n1 = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n2 = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n3 = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    n1->checkQuorum_ = true;
    n2->checkQuorum_ = true;
    n3->checkQuorum_ = true;

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);
    n3->becomeFollower(1, kNone);

    NetworkSPtr nt(new Network({n1, n2, n3}));

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // check state
    // n1.state == StateLeader
    // n2.state == StateFollower
    // n3.state == StateFollower
    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kFollower);

    nt->Isolate(3);
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});
    n1->preVote_ = true;
    n2->preVote_ = true;
    n3->preVote_ = true;
    nt->Recover();
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // check state
    // n1.state == StateLeader
    // n2.state == StateFollower
    // n3.state == StatePreCandidate
    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kPreCandidate);
    // check term
    // n1.Term == 2
    // n2.Term == 2
    // n3.Term == 2
    ASSERT_EQ(n1->term_, 2);
    ASSERT_EQ(n2->term_, 2);
    ASSERT_EQ(n3->term_, 2);

    nt->Send({idl::Message().from(3).to(3).term(n1->term_).type(idl::MsgHeartbeat)});
    ASSERT_EQ(n1->state_, StateType::kLeader);
  }

  static void TestReadOnlyOptionSafe() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    NetworkSPtr nt(new Network({a, b, c}));
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 1);

    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);

    struct TestData {
      Raft *sm;
      int proposals;
      uint64_t wri;
      std::string wctx;
    } tests[] = {
        {a, 10, 11, "ctx1"},
        {b, 10, 21, "ctx2"},
        {c, 10, 31, "ctx3"},
        {a, 10, 41, "ctx4"},
        {b, 10, 51, "ctx5"},
        {c, 10, 61, "ctx6"},
    };

    for (auto tt : tests) {
      for (int j = 0; j < tt.proposals; j++) {
        nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
      }

      nt->Send({idl::Message().from(tt.sm->id_).to(tt.sm->id_).type(idl::MsgReadIndex).entries({idl::Entry().data(tt.wctx)})});

      auto r = tt.sm;
      ASSERT_NE(r->readStates_.size(), 0);
      auto rs = r->readStates_[0];
      ASSERT_EQ(rs.index, tt.wri);
      ASSERT_EQ(rs.requestCtx, tt.wctx);
      r->readStates_ = {};
    }
  }

  static void TestReadOnlyOptionLease() {
    auto a = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto b = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto c = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());
    a->readOnly_->option = kReadOnlyLeaseBased;
    b->readOnly_->option = kReadOnlyLeaseBased;
    c->readOnly_->option = kReadOnlyLeaseBased;
    a->checkQuorum_ = true;
    b->checkQuorum_ = true;
    c->checkQuorum_ = true;

    NetworkSPtr nt(new Network({a, b, c}));
    setRandomizedElectionTimeout(b, b->electionTimeout_ + 1);

    for (uint64_t i = 0; i < b->electionTimeout_; i++) {
      b->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    ASSERT_EQ(a->state_, StateType::kLeader);

    struct TestData {
      Raft *sm;
      int proposals;
      uint64_t wri;
      std::string wctx;
    } tests[] = {
        {a, 10, 11, "ctx1"},
        {b, 10, 21, "ctx2"},
        {c, 10, 31, "ctx3"},
        {a, 10, 41, "ctx4"},
        {b, 10, 51, "ctx5"},
        {c, 10, 61, "ctx6"},
    };

    for (auto tt : tests) {
      for (int j = 0; j < tt.proposals; j++) {
        nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
      }

      nt->Send({idl::Message().from(tt.sm->id_).to(tt.sm->id_).type(idl::MsgReadIndex).entries({idl::Entry().data(tt.wctx)})});

      auto r = tt.sm;
      ASSERT_NE(r->readStates_.size(), 0);
      auto rs = r->readStates_[0];
      ASSERT_EQ(rs.index, tt.wri);
      ASSERT_EQ(rs.requestCtx, tt.wctx);
      r->readStates_ = {};
    }
  }

  // TestReadOnlyForNewLeader ensures that a leader only accepts MsgReadIndex message
  // when it commits at least one log entry at it term.
  static void TestReadOnlyForNewLeader() {
    struct NodeConfig {
      uint64_t id, committed, applied, compactIndex;
    } nodeConfigs[] = {
        {1, 1, 1, 0},
        {2, 2, 2, 2},
        {3, 2, 2, 2},
    };

    std::vector<StateMachine *> peers;
    for (auto c : nodeConfigs) {
      auto storage = new MemoryStorage;
      storage->Append(idl::EntryVec{idl::Entry().index(1).term(1),
                                    idl::Entry().index(2).term(1)});
      storage->SetHardState(idl::HardState().term(1).commit(c.committed));
      if (c.compactIndex != 0) {
        storage->Compact(c.compactIndex);
      }
      auto cfg = newTestConfig(c.id, {1, 2, 3}, 10, 1, storage);
      cfg->applied = c.applied;
      auto raft = Raft::New(cfg);
      peers.push_back(raft);
    }
    NetworkSPtr nt(new Network(peers));

    // Drop MsgApp to forbid peer a to commit any log entry at its term after it becomes leader.
    nt->Ignore(idl::MsgApp);
    // Force peer a to become leader.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto sm = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);

    // Ensure peer a drops read only request.
    uint64_t windex = 4;
    std::string wctx = "ctx";
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgReadIndex).entries({idl::Entry().data(wctx)})});
    ASSERT_EQ(sm->readStates_.size(), 0);

    nt->Recover();

    // Force peer a to commit a log entry at its term
    for (uint64_t i = 0; i < sm->heartbeatTimeout_; i++) {
      sm->tick();
    }
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    ASSERT_EQ(sm->raftLog_->committed_, 4);
    auto lastLogTerm = sm->raftLog_->zeroTermOnErrCompacted(sm->raftLog_->term(sm->raftLog_->committed_));
    ASSERT_EQ(lastLogTerm, sm->term_);

    // Ensure peer a accepts read only request after it commits a entry at its term.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgReadIndex).entries({idl::Entry().data(wctx)})});
    ASSERT_EQ(sm->readStates_.size(), 1);
    auto rs = sm->readStates_[0];
    ASSERT_EQ(rs.index, windex);
    ASSERT_EQ(rs.requestCtx, wctx);
  }

  void TestOldMessages() {
    NetworkSPtr tt(new Network({nullptr, nullptr, nullptr}));
    // make 0 leader @ term 3
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    tt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});
    tt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
    // pretend we're an old leader trying to make progress; this entry is expected to be ignored.
    tt->Send({idl::Message().from(2).to(1).type(idl::MsgApp).term(2).entries({newEntry(3, 2)})});
    // commit a new entry
    tt->Send({idl::Message().from(2).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")})});

    auto s = new MemoryStorage;
    s->ents_ = idl::EntryVec{{}, idl::Entry().index(1).term(1).data(""), idl::Entry().index(2).term(2).data(""), idl::Entry().index(3).term(3).data(""), idl::Entry().index(4).term(3).data("somedata")};

    std::unique_ptr<RaftLog> ilog(new RaftLog(s));
    ilog->unstable_.offset = 5;
    ilog->committed_ = 4;
    auto base = ilog->ltoa();

    for (auto kv : tt->peers_) {
      auto sm = static_cast<Raft *>(kv.second);
      ASSERT_EQ(base, sm->raftLog_->ltoa());
    }
  }

  // TestCampaignWhileLeader ensures that a leader node won't step down
  // when it elects itself.
  static void testCampaignWhileLeader(bool preVote) {
    auto cfg = newTestConfig(1, {1}, 5, 1, new MemoryStorage());
    cfg->preVote = preVote;
    RaftUPtr r(Raft::New(cfg));

    ASSERT_EQ(r->state_, StateType::kFollower);

    // We don't call campaign() directly because it comes after the check
    // for our current state.
    r->Step(idl::Message().from(1).to(1).type(idl::MsgHup));
    ASSERT_EQ(r->state_, StateType::kLeader);

    uint64_t term = r->term_;
    r->Step(idl::Message().from(1).to(1).type(idl::MsgHup));
    ASSERT_EQ(r->state_, StateType::kLeader);
    ASSERT_EQ(term, r->term_);
  }

  // TestCommitAfterRemoveNode verifies that pending commands can become
  // committed when a config change reduces the quorum requirements.
  static void TestCommitAfterRemoveNode() {
    // Create a cluster with two nodes.
    auto s = new MemoryStorage();
    RaftUPtr r(newTestRaft(1, {1, 2}, 5, 1, s));
    r->becomeCandidate();
    r->becomeLeader();

    // Begin to remove the second node.
    auto cc = idl::ConfChange().type(idl::ConfChangeRemoveNode).nodeid(2);
    auto ccData = cc.Marshal();
    r->Step(idl::Message()
                .type(idl::MsgProp)
                .entries({idl::Entry().type(idl::EntryConfChange).data(ccData)}));

    // Stabilize the log and make sure nothing is committed yet.
    ASSERT_FALSE(nextEnts(r.get(), s).size() > 0);

    uint64_t ccIndex = r->raftLog_->lastIndex();

    // While the config change is pending, make another proposal.
    r->Step(idl::Message().type(idl::MsgProp).entries({idl::Entry().type(idl::EntryNormal).data("hello")}));

    // Node 2 acknowledges the config change, committing it.
    r->Step(idl::Message().from(2).type(idl::MsgAppResp).index(ccIndex));

    auto ents = nextEnts(r.get(), s);
    ASSERT_EQ(ents.size(), 2);
    ASSERT_EQ(ents[0].type(), idl::EntryNormal);
    ASSERT_EQ(ents[0].data(), "");
    ASSERT_EQ(ents[1].type(), idl::EntryConfChange);

    // Apply the config change. This reduces quorum requirements so the
    // pending command can now commit.
    r->removeNode(2);
    ents = nextEnts(r.get(), s);
    ASSERT_EQ(ents.size(), 1);
    ASSERT_EQ(ents[0].type(), idl::EntryNormal);
  }

  static void TestRestore() {
    auto snap = idl::Snapshot()
                    .metadata_index(11)  // magic number
                    .metadata_term(11)   // magic number
                    .metadata_conf_state(idl::ConfState().nodes({1, 2, 3}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    ASSERT_TRUE(r->restore(snap));

    ASSERT_EQ(r->raftLog_->lastIndex(), snap.metadata_index());
    ASSERT_EQ(r->raftLog_->term(snap.metadata_index()).get_value(), snap.metadata_term());

    auto sg = r->nodes();
    ASSERT_EQ(sg, snap.metadata_conf_state().nodes());

    ASSERT_FALSE(r->restore(snap));
  }

  // TestRestoreWithLearner restores a snapshot which contains learners.
  static void TestRestoreWithLearner() {
    auto snap = idl::Snapshot()
                    .metadata_index(11)  // magic number
                    .metadata_term(11)   // magic number
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}).learners({3}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestLearnerRaft(1, {1, 2}, {3}, 8, 1, storage));
    ASSERT_TRUE(r->restore(snap));

    ASSERT_EQ(r->raftLog_->lastIndex(), snap.metadata_index());
    ASSERT_EQ(r->raftLog_->term(snap.metadata_index()).get_value(), snap.metadata_term());

    auto sg = r->nodes();
    ASSERT_EQ(sg, snap.metadata_conf_state().nodes());

    auto lns = r->learnerNodes();
    ASSERT_EQ(lns, snap.metadata_conf_state().learners());

    for (uint64_t n : snap.metadata_conf_state().nodes()) {
      ASSERT_FALSE(r->prs_[n]->isLearner_);
    }
    for (uint64_t n : snap.metadata_conf_state().learners()) {
      ASSERT_TRUE(r->learnerPrs_[n]->isLearner_);
    }

    ASSERT_FALSE(r->restore(snap));
  }

  // TestRestoreInvalidLearner verfies that a normal peer can't become learner again
  // when restores snapshot.
  static void TestRestoreInvalidLearner() {
    auto snap = idl::Snapshot()
                    .metadata_index(11)  // magic number
                    .metadata_term(11)   // magic number
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}).learners({3}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(3, {1, 2, 3}, 10, 1, storage));

    ASSERT_FALSE(r->isLearner_);
    ASSERT_FALSE(r->restore(snap));
  }

  // TestRestoreLearnerPromotion checks that a learner can become to a follower after
  // restoring snapshot.
  static void TestRestoreLearnerPromotion() {
    auto snap = idl::Snapshot()
                    .metadata_index(11)  // magic number
                    .metadata_term(11)   // magic number
                    .metadata_conf_state(idl::ConfState().nodes({1, 2, 3}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestLearnerRaft(3, {1, 2}, {3}, 10, 1, storage));

    ASSERT_TRUE(r->isLearner_);
    ASSERT_TRUE(r->restore(snap));
    ASSERT_FALSE(r->isLearner_);
  }

  // TestLearnerReceiveSnapshot tests that a learner can receive a snpahost from leader
  static void TestLearnerReceiveSnapshot() {
    // restore the state machine from a snapshot so it has a compacted log and a snapshot
    auto snap = idl::Snapshot()
                    .metadata_index(11)  // magic number
                    .metadata_term(11)   // magic number
                    .metadata_conf_state(idl::ConfState().nodes({1}).learners({2}));

    auto n1 = newTestLearnerRaft(1, {1}, {2}, 10, 1, new MemoryStorage);
    auto n2 = newTestLearnerRaft(2, {1}, {2}, 10, 1, new MemoryStorage);

    n1->restore(snap);

    // Force set n1 appplied index.
    n1->raftLog_->appliedTo(n1->raftLog_->committed_);

    NetworkSPtr nt(new Network({n1, n2}));

    setRandomizedElectionTimeout(n1, n1->electionTimeout_);
    for (int i = 0; i < n2->electionTimeout_; i++) {
      n1->tick();
    }

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgBeat)});

    ASSERT_EQ(n2->raftLog_->committed_, n1->raftLog_->committed_);
  }

  static void TestRestoreIgnoreSnapshot() {
    idl::EntryVec previousEnts = {idl::Entry().index(1).term(1),
                                  idl::Entry().index(2).term(1),
                                  idl::Entry().index(3).term(1)};
    uint64_t commit = 1;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->raftLog_->append(previousEnts);
    r->raftLog_->commitTo(commit);

    auto snap = idl::Snapshot()
                    .metadata_index(commit)
                    .metadata_term(1)
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}));

    // ignore snapshot
    ASSERT_FALSE(r->restore(snap));
    ASSERT_EQ(r->raftLog_->committed_, commit);

    // ignore snapshot and fast forward commit
    snap.metadata_index(commit + 1);
    ASSERT_FALSE(r->restore(snap));
    ASSERT_EQ(r->raftLog_->committed_, commit + 1);
  }

  static void TestProvideSnap() {
    // restore the state machine from a snapshot so it has a compacted log and a snapshot
    auto snap = idl::Snapshot()
                    .metadata_index(11)
                    .metadata_term(11)
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));
    r->restore(snap);

    r->becomeCandidate();
    r->becomeLeader();

    // force set the next of node 2, so that node 2 needs a snapshot
    r->prs_[2]->Next(r->raftLog_->firstIndex());

    r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(r->prs_[2]->next_ - 1).reject(true));

    auto msgs = r->readMessages();
    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].type(), idl::MsgSnap);
  }

  static void TestIgnoreProvidingSnap() {
    // restore the state machine from a snapshot so it has a compacted log and a snapshot
    auto snap = idl::Snapshot()
                    .metadata_index(11)
                    .metadata_term(11)
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}));

    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));
    r->restore(snap);

    r->becomeCandidate();
    r->becomeLeader();

    // force set the next of node 2, so that node 2 needs a snapshot
    // change node 2 to be inactive, expect node 1 ignore sending snapshot to 2
    r->prs_[2]->Next(r->raftLog_->firstIndex() - 1);
    r->prs_[2]->recentActive_ = false;

    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));

    auto msgs = r->readMessages();
    ASSERT_EQ(msgs.size(), 0);
  }

  static void TestRestoreFromSnapMsg() {
    // restore the state machine from a snapshot so it has a compacted log and a snapshot
    auto snap = idl::Snapshot()
                    .metadata_index(11)
                    .metadata_term(11)
                    .metadata_conf_state(idl::ConfState().nodes({1, 2}));

    auto m = idl::Message().from(1).type(idl::MsgSnap).term(2).snapshot(snap);

    RaftUPtr r(newTestRaft(1, {1}, 10, 1, new MemoryStorage));
    r->Step(m);

    ASSERT_EQ(r->lead_, 1);
  }

  void TestSlowNodeRestore() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);
    for (int j = 0; j < 100; j++) {
      nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    }
    auto lead = static_cast<Raft *>(nt->peers_[1]);
    nextEnts(lead, nt->storage_[1]);
    auto cs = new idl::ConfState();
    cs->nodes(lead->nodes());
    nt->storage_[1]->CreateSnapshot(lead->raftLog_->applied_, cs, "");
    nt->storage_[1]->Compact(lead->raftLog_->applied_);

    nt->Recover();
    // send heartbeats so that the leader can learn everyone is active.
    // node 3 will only be considered as active when node 1 receives a reply from it.
    while (true) {
      nt->Send({idl::Message().from(1).to(1).type(idl::MsgBeat)});
      if (lead->prs_[3]->recentActive_) {
        break;
      }
    }

    // trigger a snapshot
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});

    auto follower = static_cast<Raft *>(nt->peers_[3]);

    // trigger a commit
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    ASSERT_EQ(follower->raftLog_->committed_, lead->raftLog_->committed_);
  }

  // TestStepConfig tests that when raft step msgProp in EntryConfChange type,
  // it appends the entry to log and sets pendingConf to be true.
  static void TestStepConfig() {
    // a raft that cannot make progress
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    uint64_t index = r->raftLog_->lastIndex();
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().type(idl::EntryConfChange)}));

    ASSERT_EQ(r->raftLog_->lastIndex(), index + 1);
    ASSERT_EQ(r->pendingConfIndex_, index + 1);
  }

  // TestStepIgnoreConfig tests that if raft step the second msgProp in
  // EntryConfChange type when the first one is uncommitted, the node will set
  // the proposal to noop and keep its original state.
  static void TestStepIgnoreConfig() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().type(idl::EntryConfChange)}));
    uint64_t index = r->raftLog_->lastIndex();
    uint64_t pendingConfIndex = r->pendingConfIndex_;
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().type(idl::EntryConfChange)}));
    idl::EntryVec wents{idl::Entry().type(idl::EntryNormal).term(1).index(3).data("")};
    auto errWithEnts = r->raftLog_->entries(index + 1, noLimit);
    ASSERT_TRUE(errWithEnts.is_ok());
    EntryVec_ASSERT_EQ(errWithEnts.get_value(), wents);
    ASSERT_EQ(r->pendingConfIndex_, pendingConfIndex);
  }

  // TestNewLeaderPendingConfig tests that new leader sets its pendingConfigIndex
  // based on uncommitted entries.
  static void TestNewLeaderPendingConfig() {
    struct TestData {
      bool addEntry;
      uint64_t wpendingIndex;
    } tests[] = {
        {false, 0},
        {true, 1},
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
      if (tt.addEntry) {
        r->appendEntry(idl::EntryVec{idl::Entry().type(idl::EntryNormal)});
      }
      r->becomeCandidate();
      r->becomeLeader();
      ASSERT_EQ(r->pendingConfIndex_, tt.wpendingIndex);
    }
  }

  void TestLeaderAppResp() {
    // initial progress: match = 0; next = 3
    struct TestData {
      uint64_t index;
      bool reject;
      // progress
      uint64_t wmatch;
      uint64_t wnext;
      // message
      int wmsgNum;
      uint64_t windex;
      uint64_t wcommitted;
    } tests[] = {
        {3, true, 0, 3, 0, 0, 0},   // stale resp; no replies
        {2, true, 0, 2, 1, 1, 0},   // denied resp; leader does not commit; decrease next and send probing msg
        {2, false, 2, 4, 2, 2, 2},  // accept resp; leader commits; broadcast with commit index
        {0, false, 0, 3, 0, 0, 0},  // ignore heartbeat replies
    };

    for (auto tt : tests) {
      // sm term is 1 after it becomes the leader.
      // thus the last log term must be 1 to be committed.
      RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage));
      auto s = new MemoryStorage;
      s->ents_ = {{}, newEntry(1, 0), newEntry(2, 1)};
      auto l = new RaftLog(s);
      l->unstable_.offset = 3;
      sm->raftLog_.reset(l);

      sm->becomeCandidate();
      sm->becomeLeader();
      sm->readMessages();
      sm->Step(idl::Message().from(2).type(idl::MsgAppResp).index(tt.index).term(sm->term_).reject(tt.reject).rejecthint(tt.index));

      auto p = sm->prs_[2];
      ASSERT_EQ(p->match_, tt.wmatch);
      ASSERT_EQ(p->next_, tt.wnext);

      auto msgs = sm->readMessages();
      ASSERT_EQ(msgs.size(), tt.wmsgNum);

      for (auto msg : msgs) {
        ASSERT_EQ(msg.index(), tt.windex);
        ASSERT_EQ(msg.commit(), tt.wcommitted);
      }
    }
  }

  // When the leader receives a heartbeat tick, it should
  // send a MsgApp with m.Index = 0, m.LogTerm=0 and empty entries.
  void TestBcastBeat() {
    uint64_t offset = 1000;
    // make a state machine with log.offset = 1000
    auto s = idl::Snapshot().metadata_index(offset).metadata_term(1).metadata_conf_state(idl::ConfState().nodes({1, 2, 3}));
    auto storage = new MemoryStorage();
    storage->ApplySnapshot(s);
    RaftUPtr sm(newTestRaft(1, {}, 10, 1, storage));
    sm->term_ = 1;

    sm->becomeCandidate();
    sm->becomeLeader();
    for (uint64_t i = 0; i < 10; i++) {
      auto ents = idl::EntryVec{idl::Entry().index(i + 1)};
      sm->appendEntry(ents);
    }
    // slow follower
    sm->prs_[2]->match_ = 5, sm->prs_[2]->next_ = 6;
    // normal follower
    sm->prs_[3]->match_ = sm->raftLog_->lastIndex(), sm->prs_[2]->next_ = sm->raftLog_->lastIndex() + 1;

    sm->Step(idl::Message().type(idl::MsgBeat));
    auto msgs = sm->readMessages();
    ASSERT_EQ(msgs.size(), 2);
    std::map<uint64_t, uint64_t> wantCommitMap = {
        {2, std::min(sm->raftLog_->committed_, sm->prs_[2]->match_)},
        {3, std::min(sm->raftLog_->committed_, sm->prs_[3]->match_)},
    };
    for (auto m : msgs) {
      ASSERT_EQ(m.type(), idl::MsgHeartbeat);
      ASSERT_EQ(m.index(), 0);
      ASSERT_EQ(m.logterm(), 0);
      ASSERT_NE(wantCommitMap[m.to()], 0);
      ASSERT_EQ(m.commit(), wantCommitMap[m.to()]);
      wantCommitMap.erase(m.to());
      ASSERT_EQ(m.entries().size(), 0);
    }
  }

  // tests the output of the state machine when receiving MsgBeat
  void TestRecvMsgBeat() {
    struct TestData {
      StateType state;
      int wMsg;
    } tests[] = {
        {StateType::kLeader, 2},
        // candidate and follower should ignore MsgBeat
        {StateType::kCandidate, 0},
        {StateType::kFollower, 0},
    };

    for (auto tt : tests) {
      RaftUPtr sm(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage));
      auto s = new MemoryStorage;
      s->ents_ = {{}, newEntry(1, 0), newEntry(2, 1)};
      sm->raftLog_.reset(new RaftLog(s));
      sm->term_ = 1;
      sm->state_ = tt.state;
      switch (tt.state) {
        case StateType::kFollower:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepFollower(m); };
          break;
        case StateType::kCandidate:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepCandidate(m); };
          break;
        case StateType::kLeader:
          sm->step_ = [&](idl::Message &m) -> error_s { return sm->stepLeader(m); };
          break;
        default:
          break;
      }
      sm->Step(idl::Message().from(1).to(1).type(idl::MsgBeat));

      auto msgs = sm->readMessages();
      ASSERT_EQ(msgs.size(), tt.wMsg);
      for (auto m : msgs) {
        ASSERT_EQ(m.type(), idl::MsgHeartbeat);
      }
    }
  }

  void TestLeaderIncreaseNext() {
    idl::EntryVec previousEnts = {idl::Entry().index(1).term(1),
                                  idl::Entry().index(2).term(1),
                                  idl::Entry().index(3).term(1)};

    struct TestData {
      Progress::StateType state;
      uint64_t next;
      uint64_t wnext;
    } tests[] = {
        // state replicate, optimistically increase next
        // previous entries + noop entry + propose + 1
        {Progress::kStateReplicate, 2, previousEnts.size() + 1 + 1 + 1},
        // state probe, not optimistically increase next
        {Progress::kStateProbe, 2, 2},
    };

    for (auto tt : tests) {
      RaftUPtr sm(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
      sm->raftLog_->append(previousEnts);

      sm->becomeCandidate();
      sm->becomeLeader();
      sm->prs_[2]->state_ = tt.state;
      sm->prs_[2]->next_ = tt.next;
      sm->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("some data")}));

      ASSERT_EQ(sm->prs_[2]->next_, tt.wnext);
    }
  }

  void TestSendAppendForProgressProbe() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->readMessages();
    r->prs_[2]->becomeProbe();

    // each round is a heartbeat
    for (int i = 0; i < 3; i++) {
      if (i == 0) {
        // we expect that raft will only send out one msgAPP on the first
        // loop. After that, the follower is paused until a heartbeat response is
        // received.
        r->appendEntry(idl::EntryVec{idl::Entry().data("somedata")});
        r->sendAppend(2);
        auto msg = r->readMessages();
        ASSERT_EQ(msg.size(), 1);
        ASSERT_EQ(msg[0].index(), 0);
      }

      ASSERT_TRUE(r->prs_[2]->paused_);
      for (int j = 0; j < 10; j++) {
        r->appendEntry(idl::EntryVec{idl::Entry().data("somedata")});
        r->sendAppend(2);
        ASSERT_EQ(r->readMessages().size(), 0);
      }

      // do a heartbeat
      for (uint64_t j = 0; j < r->heartbeatTimeout_; j++) {
        r->Step(idl::Message().from(1).to(1).type(idl::MsgBeat));
      }
      ASSERT_TRUE(r->prs_[2]->paused_);

      // consume the heartbeat
      auto msg = r->readMessages();
      ASSERT_EQ(msg.size(), 1);
      ASSERT_EQ(msg[0].type(), idl::MsgHeartbeat);
    }

    // a heartbeat response will allow another message to be sent
    r->Step(idl::Message().from(2).to(1).type(idl::MsgHeartbeatResp));
    auto msg = r->readMessages();
    ASSERT_EQ(msg.size(), 1);
    ASSERT_EQ(msg[0].index(), 0);
    ASSERT_TRUE(r->prs_[2]->paused_);
  }

  void TestSendAppendForProgressReplicate() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->readMessages();
    r->prs_[2]->becomeReplicate();

    // each round is a heartbeat
    for (int i = 0; i < 10; i++) {
      r->appendEntry(idl::EntryVec{idl::Entry().data("somedata")});
      r->sendAppend(2);
      auto msgs = r->readMessages();
      ASSERT_EQ(msgs.size(), 1);
    }
  }

  void TestSendAppendForProgressSnapshot() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->readMessages();
    r->prs_[2]->becomeSnapshot(10);

    for (int i = 0; i < 10; i++) {
      r->appendEntry(idl::EntryVec{idl::Entry().data("somedata")});
      r->sendAppend(2);
      auto msgs = r->readMessages();
      ASSERT_EQ(msgs.size(), 0);
    }
  }

  void TestRecvMsgUnreachable() {
    idl::EntryVec previousEnts = {idl::Entry().index(1).term(1),
                                  idl::Entry().index(2).term(1),
                                  idl::Entry().index(3).term(1)};
    auto storage = new MemoryStorage;
    storage->Append(previousEnts);
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->becomeCandidate();
    r->becomeLeader();
    r->readMessages();

    // set node 2 to state replicate
    r->prs_[2]->Match(3);
    r->prs_[2]->becomeReplicate();
    r->prs_[2]->optimisticUpdate(5);

    r->Step(idl::Message().from(2).to(1).type(idl::MsgUnreachable));
    ASSERT_EQ(r->prs_[2]->state_, Progress::kStateProbe);
    ASSERT_EQ(r->prs_[2]->match_ + 1, r->prs_[2]->next_);
  }
  //
  // TestAddNode tests that addNode could update nodes correctly.
  static void TestAddNode() {
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, new MemoryStorage));
    r->addNode(2);

    auto nodes = r->nodes();
    std::vector<uint64_t> wnodes{1, 2};
    ASSERT_EQ(wnodes, nodes);
  }

  // TestAddLearner tests that addLearner could update nodes correctly.
  static void TestAddLearner() {
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, new MemoryStorage));
    r->addLearner(2);

    auto nodes = r->learnerNodes();
    std::vector<uint64_t> wnodes{2};
    ASSERT_EQ(wnodes, nodes);
    ASSERT_EQ(r->learnerPrs_[2]->isLearner_, true);
  }

  static void TestAddNodeCheckQuorum() {
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, new MemoryStorage));
    r->checkQuorum_ = true;

    r->becomeCandidate();
    r->becomeLeader();

    for (uint64_t i = 0; i < r->electionTimeout_ - 1; i++) {
      r->tick();
    }

    r->addNode(2);

    // This tick will reach electionTimeout, which triggers a quorum check.
    r->tick();

    // Node 1 should still be the leader after a single tick.
    ASSERT_EQ(r->state_, StateType::kLeader);

    // After another electionTimeout ticks without hearing from node 2,
    // node 1 should step down.
    for (uint64_t i = 0; i < r->electionTimeout_; i++) {
      r->tick();
    }

    ASSERT_EQ(r->state_, StateType::kFollower);
  }

  // TestRemoveNode tests that removeNode could update nodes and
  // and removed list correctly.
  static void TestRemoveNode() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, new MemoryStorage));
    r->removeNode(2);

    ASSERT_EQ(r->nodes(), std::vector<uint64_t>({1}));

    // remove all nodes from cluster
    r->removeNode(1);
    ASSERT_EQ(r->nodes(), std::vector<uint64_t>({}));
  }

  // TestRemoveLearner tests that removeNode could update nodes and
  // and removed list correctly.
  static void TestRemoveLearner() {
    RaftUPtr r(newTestLearnerRaft(1, {1}, {2}, 10, 1, new MemoryStorage));
    r->removeNode(2);
    ASSERT_EQ(r->nodes(), std::vector<uint64_t>({1}));

    ASSERT_EQ(r->learnerNodes(), std::vector<uint64_t>({}));

    // remove all nodes from cluster
    r->removeNode(1);
    ASSERT_EQ(r->nodes(), std::vector<uint64_t>({}));
  }

  static void TestPromotable() {
    uint64_t id = 1;
    struct TestData {
      std::vector<uint64_t> peers;
      bool wp;
    } tests[] = {
        {{1}, true},
        {{1, 2, 3}, true},
        {{}, false},
        {{2, 3}, false},
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(id, tt.peers, 5, 1, new MemoryStorage));
      ASSERT_EQ(r->promotable(), tt.wp);
    }
  }

  static void TestRaftNodes() {
    struct TestData {
      std::vector<uint64_t> ids;
      std::vector<uint64_t> wids;
    } tests[] = {
        {
            {1, 2, 3},
            {1, 2, 3},
        },
        {
            {3, 2, 1},
            {1, 2, 3},
        },
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, tt.ids, 10, 1, new MemoryStorage));
      ASSERT_EQ(r->nodes(), tt.wids);
    }
  }

  void checkLeaderTransferState(Raft *r, StateType state, uint64_t lead) {
    ASSERT_EQ(r->state_, state);
    ASSERT_EQ(r->lead_, lead);
    ASSERT_EQ(r->leadTransferee_, kNone);
  }

  // TestLeaderTransferToUpToDateNode verifies transferring should succeed
  // if the transferee has the most up-to-date log entries when transfer starts.
  void TestLeaderTransferToUpToDateNode() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    ASSERT_EQ(lead->lead_, 1);

    // Transfer leadership to 2.
    nt->Send({idl::Message().from(2).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kFollower, 2);

    // After some log replication, transfer leadership back to 1.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    nt->Send({idl::Message().from(1).to(2).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  // TestLeaderTransferToUpToDateNodeFromFollower verifies transferring should succeed
  // if the transferee has the most up-to-date log entries when transfer starts.
  // Not like TestLeaderTransferToUpToDateNode, where the leader transfer message
  // is sent to the leader, in this test case every leader transfer message is sent
  // to the follower.
  void TestLeaderTransferToUpToDateNodeFromFollower() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    ASSERT_EQ(lead->lead_, 1);

    // Transfer leadership to 2.
    nt->Send({idl::Message().from(2).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kFollower, 2);

    // After some log replication, transfer leadership back to 1.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  // TestLeaderTransferWithCheckQuorum ensures transferring leader still works
  // even the current leader is still under its leader lease
  void TestLeaderTransferWithCheckQuorum() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    for (uint64_t i = 1; i < 4; i++) {
      auto r = static_cast<Raft *>(nt->peers_[i]);
      r->checkQuorum_ = true;
      setRandomizedElectionTimeout(r, r->electionTimeout_ + i);
    }

    // Letting peer 2 electionElapsed reach to timeout so that it can vote for peer 1
    auto f = static_cast<Raft *>(nt->peers_[2]);
    for (uint64_t i = 0; i < f->electionTimeout_; i++) {
      f->tick();
    }

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto lead = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(lead->lead_, 1);

    // Transfer leadership to 2.
    nt->Send({idl::Message().from(2).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kFollower, 2);

    // After some log replication, transfer leadership back to 1.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    nt->Send({idl::Message().from(1).to(2).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  void TestLeaderTransferToSlowFollower() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});

    nt->Recover();
    auto lead = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(lead->prs_[3]->match_, 1);

    // Transfer leadership to 3 when node 3 is lack of log.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kFollower, 3);
  }

  void TestLeaderTransferAfterSnapshot() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    auto lead = static_cast<Raft *>(nt->peers_[1]);
    nextEnts(lead, nt->storage_[1]);

    auto cs = new idl::ConfState();
    cs->nodes(lead->nodes());
    nt->storage_[1]->CreateSnapshot(lead->raftLog_->applied_, cs, "");
    nt->storage_[1]->Compact(lead->raftLog_->applied_);

    nt->Recover();
    ASSERT_EQ(lead->prs_[3]->match_, 1);

    // Transfer leadership to 3 when node 3 is lack of snapshot.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    // Send pb.MsgHeartbeatResp to leader to trigger a snapshot for node 3.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgHeartbeatResp)});

    checkLeaderTransferState(lead, StateType::kFollower, 3);
  }

  void TestLeaderTransferToSelf() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // Transfer leadership to self, there will be noop.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgTransferLeader)});
    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  void TestLeaderTransferToNonExistingNode() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // Transfer leadership to non-existing node, there will be noop.
    nt->Send({idl::Message().from(4).to(1).type(idl::MsgTransferLeader)});
    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  void TestLeaderTransferTimeout() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // Transfer leadership to isolated node, wait for timeout.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);
    for (uint64_t i = 0; i < lead->heartbeatTimeout_; i++) {
      lead->tick();
    }
    ASSERT_EQ(lead->leadTransferee_, 3);

    for (uint64_t i = 0; i < lead->electionTimeout_ - lead->heartbeatTimeout_; i++) {
      lead->tick();
    }
    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  void TestLeaderTransferIgnoreProposal() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // Transfer leadership to isolated node to let transfer pending, then send proposal.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()})});
    auto err = lead->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry()}));
    ASSERT_EQ(err, ErrProposalDropped);

    ASSERT_EQ(lead->prs_[1]->match_, 1);
  }

  void TestLeaderTransferReceiveHigherTermVote() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // Transfer leadership to isolated node to let transfer pending.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    nt->Send({idl::Message().from(2).to(2).type(idl::MsgHup).index(1).term(2)});

    checkLeaderTransferState(lead, StateType::kFollower, 2);
  }

  void TestLeaderTransferRemoveNode() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Ignore(idl::MsgTimeoutNow);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    // The leadTransferee is removed when leadship transferring.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    lead->removeNode(3);

    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  // TestLeaderTransferBack verifies leadership can transfer back to self when last transfer is pending.
  void TestLeaderTransferBack() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Ignore(idl::MsgTimeoutNow);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    // Transfer leadership back to self.
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  // TestLeaderTransferSecondTransferToAnotherNode verifies leader can transfer to another node
  // when last transfer is pending.
  void TestLeaderTransferSecondTransferToAnotherNode() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    // Transfer leadership to another node.
    nt->Send({idl::Message().from(2).to(1).type(idl::MsgTransferLeader)});

    checkLeaderTransferState(lead, StateType::kFollower, 2);
  }

  // TestLeaderTransferSecondTransferToSameNode verifies second transfer leader request
  // to the same node should not extend the timeout while the first one is pending.
  void TestLeaderTransferSecondTransferToSameNode() {
    NetworkSPtr nt(new Network({nullptr, nullptr, nullptr}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    nt->Isolate(3);

    auto lead = static_cast<Raft *>(nt->peers_[1]);

    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});
    ASSERT_EQ(lead->leadTransferee_, 3);

    for (uint64_t i = 0; i < lead->heartbeatTimeout_; i++) {
      lead->tick();
    }
    // Second transfer leadership request to the same node.
    nt->Send({idl::Message().from(3).to(1).type(idl::MsgTransferLeader)});

    for (uint64_t i = 0; i < lead->electionTimeout_ - lead->heartbeatTimeout_; i++) {
      lead->tick();
    }
    checkLeaderTransferState(lead, StateType::kLeader, 1);
  }

  // TestTransferNonMember verifies that when a MsgTimeoutNow arrives at
  // a node that has been removed from the group, nothing happens.
  // (previously, if the node also got votes, it would panic as it
  // transitioned to StateLeader)
  static void TestTransferNonMember() {
    RaftUPtr r(newTestRaft(1, {2, 3, 4}, 5, 1, new MemoryStorage));
    r->Step(idl::Message().from(2).to(1).type(idl::MsgTimeoutNow));

    r->Step(idl::Message().from(2).to(1).type(idl::MsgVoteResp));
    r->Step(idl::Message().from(3).to(1).type(idl::MsgVoteResp));
    ASSERT_EQ(r->state_, StateType::kFollower);
  }

  // TestNodeWithSmallerTermCanCompleteElection tests the scenario where a node
  // that has been partitioned away (and fallen behind) rejoins the cluster at
  // about the same time the leader node gets partitioned away.
  // Previously the cluster would come to a standstill when run with PreVote
  // enabled.
  static void TestNodeWithSmallerTermCanCompleteElection() {
    auto n1 = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n2 = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n3 = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);
    n3->becomeFollower(1, kNone);

    n1->preVote_ = true;
    n2->preVote_ = true;
    n3->preVote_ = true;

    // cause a network partition to isolate node 3
    NetworkSPtr nt(new Network({n1, n2, n3}));
    nt->Cut(1, 3);
    nt->Cut(2, 3);

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    auto sm = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kLeader);

    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->state_, StateType::kFollower);

    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kPreCandidate);

    nt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});

    // check whether the term values are expected
    // a.Term == 3
    // b.Term == 3
    // c.Term == 1
    sm = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(sm->term_, 3);
    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->term_, 3);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->term_, 1);

    // check state
    // a == follower
    // b == leader
    // c == pre-candidate
    sm = static_cast<Raft *>(nt->peers_[1]);
    ASSERT_EQ(sm->state_, StateType::kFollower);
    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->state_, StateType::kLeader);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kPreCandidate);

    RAFT_LOG(INFO, "going to bring back peer 3 and kill peer 2");
    // recover the network then immediately isolate b which is currently
    // the leader, this is to emulate the crash of b.
    nt->Recover();
    nt->Cut(2, 1);
    nt->Cut(2, 3);

    // call for election
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // do we have a leader?
    auto sma = static_cast<Raft *>(nt->peers_[1]);
    auto smb = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_FALSE(sma->state_ != StateType::kLeader && smb->state_ != StateType::kLeader);
  }

  // TestPreVoteWithSplitVote verifies that after split vote, cluster can complete
  // election in next round.
  static void TestPreVoteWithSplitVote() {
    auto n1 = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n2 = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n3 = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);
    n3->becomeFollower(1, kNone);

    n1->preVote_ = true;
    n2->preVote_ = true;
    n3->preVote_ = true;

    NetworkSPtr nt(new Network({n1, n2, n3}));
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // simulate leader down. followers start split vote.
    nt->Isolate(1);
    nt->Send({
        idl::Message().from(2).to(2).type(idl::MsgHup),
        idl::Message().from(3).to(3).type(idl::MsgHup),
    });

    // check whether the term values are expected
    // n2.Term == 3
    // n3.Term == 3
    auto sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->term_, 3);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->term_, 3);

    // check state
    // n2 == candidate
    // n3 == candidate
    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->state_, StateType::kCandidate);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kCandidate);

    // node 2 election timeout first
    nt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});

    // check whether the term values are expected
    // n2.Term == 4
    // n3.Term == 4
    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->term_, 4);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->term_, 4);

    // check state
    // n2 == leader
    // n3 == follower
    sm = static_cast<Raft *>(nt->peers_[2]);
    ASSERT_EQ(sm->state_, StateType::kLeader);
    sm = static_cast<Raft *>(nt->peers_[3]);
    ASSERT_EQ(sm->state_, StateType::kFollower);
  }

  // simulate rolling update a cluster for Pre-Vote. cluster has 3 nodes [n1, n2, n3].
  // n1 is leader with term 2
  // n2 is follower with term 2
  // n3 is partitioned, with term 4 and less log, state is candidate
  static Network *newPreVoteMigrationCluster() {
    auto n1 = newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n2 = newTestRaft(2, {1, 2, 3}, 10, 1, new MemoryStorage());
    auto n3 = newTestRaft(3, {1, 2, 3}, 10, 1, new MemoryStorage());

    n1->becomeFollower(1, kNone);
    n2->becomeFollower(1, kNone);
    n3->becomeFollower(1, kNone);

    n1->preVote_ = true;
    n2->preVote_ = true;
    // We intentionally do not enable PreVote for n3, this is done so in order
    // to simulate a rolling restart process where it's possible to have a mixed
    // version cluster with replicas with PreVote enabled, and replicas without.

    auto nt = new Network({n1, n2, n3});
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // Cause a network partition to isolate n3.
    nt->Isolate(3);
    nt->Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("some data")})});
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    // check state
    // n1.state == StateLeader
    // n2.state == StateFollower
    // n3.state == StateCandidate
    EXPECT_EQ(n1->state_, StateType::kLeader);
    EXPECT_EQ(n2->state_, StateType::kFollower);
    EXPECT_EQ(n3->state_, StateType::kCandidate);

    // check term
    // n1.Term == 2
    // n2.Term == 2
    // n3.Term == 4
    EXPECT_EQ(n1->term_, 2);
    EXPECT_EQ(n2->term_, 2);
    EXPECT_EQ(n3->term_, 4);

    // Enable prevote on n3, then recover the network
    n3->preVote_ = true;
    nt->Recover();

    return nt;
  }

  static void TestPreVoteMigrationCanCompleteElection() {
    NetworkSPtr nt(newPreVoteMigrationCluster());

    // n1 is leader with term 2
    // n2 is follower with term 2
    // n3 is pre-candidate with term 4, and less log
    auto n2 = static_cast<Raft *>(nt->peers_[2]);
    auto n3 = static_cast<Raft *>(nt->peers_[3]);

    nt->Send({idl::Message().from(1).to(1).type(idl::MsgHup)});

    // simulate leader down
    nt->Isolate(1);

    // Call for elections from both n2 and n3.
    nt->Send({
        idl::Message().from(3).to(3).type(idl::MsgHup),
        idl::Message().from(2).to(2).type(idl::MsgHup),
    });

    // check state
    // n2.state == Follower
    // n3.state == PreCandidate
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kPreCandidate);

    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});
    nt->Send({idl::Message().from(2).to(2).type(idl::MsgHup)});

    // Do we have a leader?
    ASSERT_FALSE(n2->state_ != StateType::kLeader && n3->state_ != StateType::kLeader);
  }

  static void TestPreVoteMigrationWithFreeStuckPreCandidate() {
    NetworkSPtr nt(newPreVoteMigrationCluster());

    // n1 is leader with term 2
    // n2 is follower with term 2
    // n3 is pre-candidate with term 4, and less log
    auto n1 = static_cast<Raft *>(nt->peers_[1]);
    auto n2 = static_cast<Raft *>(nt->peers_[2]);
    auto n3 = static_cast<Raft *>(nt->peers_[3]);

    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kPreCandidate);

    // Pre-Vote again for safety
    nt->Send({idl::Message().from(3).to(3).type(idl::MsgHup)});

    ASSERT_EQ(n1->state_, StateType::kLeader);
    ASSERT_EQ(n2->state_, StateType::kFollower);
    ASSERT_EQ(n3->state_, StateType::kPreCandidate);

    nt->Send({idl::Message().from(1).to(3).type(idl::MsgHeartbeat).term(n1->term_)});

    // Disrupt the leader so that the stuck peer is freed
    ASSERT_EQ(n1->state_, StateType::kFollower);
    ASSERT_EQ(n1->term_, n3->term_);
  }
};

TEST_F(RaftTest, LeaderElection) {
  testLeaderElection(false);
}

TEST_F(RaftTest, LeaderElectionPreVote) {
  testLeaderElection(true);
}

TEST_F(RaftTest, LearnerElectionTimeout) {
  TestLearnerElectionTimeout();
}

TEST_F(RaftTest, LearnerPromotion) {
  TestLearnerPromotion();
}

TEST_F(RaftTest, LearnerCannotVote) {
  TestLearnerCannotVote();
}

TEST_F(RaftTest, LeaderCycle) {
  testLeaderCycle(false);
}

TEST_F(RaftTest, LeaderCyclePreVote) {
  testLeaderCycle(true);
}

TEST_F(RaftTest, LeaderElectionOverwriteNewerLogs) {
  testLeaderElectionOverwriteNewerLogs(false);
}

TEST_F(RaftTest, LeaderElectionOverwriteNewerLogsPreVote) {
  testLeaderElectionOverwriteNewerLogs(true);
}

TEST_F(RaftTest, VoteFromAnyState) {
  testVoteFromAnyState(idl::MsgVote);
}

TEST_F(RaftTest, PreVoteFromAnyState) {
  testVoteFromAnyState(idl::MsgPreVote);
}

TEST_F(RaftTest, LogReplication) {
  TestLogReplication();
}

TEST_F(RaftTest, LearnerLogReplication) {
  TestLearnerLogReplication();
}

TEST_F(RaftTest, SingleNodeCommit) {
  TestSingleNodeCommit();
}

TEST_F(RaftTest, CannotCommitWithoutNewTermEntry) {
  TestCannotCommitWithoutNewTermEntry();
}

TEST_F(RaftTest, CommitWithoutNewTermEntry) {
  TestCommitWithoutNewTermEntry();
}

TEST_F(RaftTest, DuelingCandidates) {
  TestDuelingCandidates();
}

TEST_F(RaftTest, DuelingPreCandidates) {
  TestDuelingPreCandidates();
}

TEST_F(RaftTest, CandidateConcede) {
  TestCandidateConcede();
}

TEST_F(RaftTest, SingleNodeCandidate) {
  TestSingleNodeCandidate();
}

TEST_F(RaftTest, SingleNodePreCandidate) {
  TestSingleNodePreCandidate();
}

TEST_F(RaftTest, OldMessages) {
  TestOldMessages();
}

TEST_F(RaftTest, Proposal) {
  TestProposal();
}

TEST_F(RaftTest, ProposalByProxy) {
  TestProposalByProxy();
}

TEST_F(RaftTest, Commit) {
  TestCommit();
}

TEST_F(RaftTest, PastElectionTimeout) {
  TestPastElectionTimeout();
}

TEST_F(RaftTest, StepIgnoreOldTermMsg) {
  TestStepIgnoreOldTermMsg();
}

TEST_F(RaftTest, HandleMsgApp) {
  TestHandleMsgApp();
}

TEST_F(RaftTest, HandleHeartbeat) {
  TestHandleHeartbeat();
}

TEST_F(RaftTest, HandleHeartbeatResp) {
  TestHandleHeartbeatResp();
}

TEST_F(RaftTest, RaftFreesReadOnlyMem) {
  TestRaftFreesReadOnlyMem();
}

TEST_F(RaftTest, MsgAppRespWaitReset) {
  TestMsgAppRespWaitReset();
}

TEST_F(RaftTest, RecvMsgVote) {
  testRecvMsgVote(idl::MsgVote);
}

TEST_F(RaftTest, RecvMsgPreVote) {
  testRecvMsgVote(idl::MsgPreVote);
}

TEST_F(RaftTest, StateTransition) {
  TestStateTransition();
}

TEST_F(RaftTest, AllServerStepdown) {
  TestAllServerStepdown();
}

TEST_F(RaftTest, CandidateResetTermMsgHeartbeat) {
  testCandidateResetTerm(idl::MsgHeartbeat);
}

TEST_F(RaftTest, CandidateResetTermMsgApp) {
  testCandidateResetTerm(idl::MsgApp);
}

TEST_F(RaftTest, LeaderStepdownWhenQuorumActive) {
  TestLeaderStepdownWhenQuorumActive();
}

TEST_F(RaftTest, LeaderStepdownWhenQuorumLost) {
  TestLeaderStepdownWhenQuorumLost();
}

TEST_F(RaftTest, LeaderSupersedingWithCheckQuorum) {
  TestLeaderSupersedingWithCheckQuorum();
}

TEST_F(RaftTest, LeaderElectionWithCheckQuorum) {
  TestLeaderElectionWithCheckQuorum();
}

TEST_F(RaftTest, FreeStuckCandidateWithCheckQuorum) {
  TestFreeStuckCandidateWithCheckQuorum();
}

TEST_F(RaftTest, NonPromotableVoterWithCheckQuorum) {
  TestNonPromotableVoterWithCheckQuorum();
}

TEST_F(RaftTest, DisruptiveFollower) {
  TestDisruptiveFollower();
}

TEST_F(RaftTest, DisruptiveFollowerPreVote) {
  TestDisruptiveFollowerPreVote();
}

TEST_F(RaftTest, ReadOnlyOptionSafe) {
  TestReadOnlyOptionSafe();
}

TEST_F(RaftTest, ReadOnlyOptionLease) {
  TestReadOnlyOptionLease();
}

TEST_F(RaftTest, ReadOnlyForNewLeader) {
  TestReadOnlyForNewLeader();
}

TEST_F(RaftTest, LeaderAppResp) {
  TestLeaderAppResp();
}

TEST_F(RaftTest, BcastBeat) {
  TestBcastBeat();
}

TEST_F(RaftTest, RecvMsgBeat) {
  TestRecvMsgBeat();
}

TEST_F(RaftTest, LeaderIncreaseNext) {
  TestLeaderIncreaseNext();
}

TEST_F(RaftTest, SendAppendForProgressProbe) {
  TestSendAppendForProgressProbe();
}

TEST_F(RaftTest, SendAppendForProgressReplicate) {
  TestSendAppendForProgressReplicate();
}

TEST_F(RaftTest, SendAppendForProgressSnapshot) {
  TestSendAppendForProgressSnapshot();
}

TEST_F(RaftTest, RecvMsgUnreachable) {
  TestRecvMsgUnreachable();
}

TEST_F(RaftTest, Restore) {
  TestRestore();
}

TEST_F(RaftTest, RestoreWithLearner) {
  TestRestoreWithLearner();
}

TEST_F(RaftTest, RestoreInvalidLearner) {
  TestRestoreInvalidLearner();
}

TEST_F(RaftTest, RestoreLearnerPromotion) {
  TestRestoreLearnerPromotion();
}

TEST_F(RaftTest, LearnerReceiveSnapshot) {
  TestLearnerReceiveSnapshot();
}

TEST_F(RaftTest, RestoreIgnoreSnapshot) {
  TestRestoreIgnoreSnapshot();
}

TEST_F(RaftTest, ProvideSnap) {
  TestProvideSnap();
}

TEST_F(RaftTest, IgnoreProvidingSnap) {
  TestIgnoreProvidingSnap();
}

TEST_F(RaftTest, RestoreFromSnapMsg) {
  TestRestoreFromSnapMsg();
}

TEST_F(RaftTest, SlowNodeRestore) {
  TestSlowNodeRestore();
}

TEST_F(RaftTest, StepConfig) {
  TestStepConfig();
}

TEST_F(RaftTest, StepIgnoreConfig) {
  TestStepIgnoreConfig();
}

TEST_F(RaftTest, NewLeaderPendingConfig) {
  TestNewLeaderPendingConfig();
}

TEST_F(RaftTest, AddNode) {
  TestAddNode();
}

TEST_F(RaftTest, AddLearner) {
  TestAddLearner();
}

//TEST_F(RaftTest, AddNodeCheckQuorum) {
//  TestAddNodeCheckQuorum();
//}
//
TEST_F(RaftTest, RemoveNode) {
  TestRemoveNode();
}

TEST_F(RaftTest, RemoveLearner) {
  TestRemoveLearner();
}

TEST_F(RaftTest, Promotable) {
  TestPromotable();
}

TEST_F(RaftTest, RaftNodes) {
  TestRaftNodes();
}

TEST_F(RaftTest, CampaignWhileLeader) {
  testCampaignWhileLeader(false);
}

TEST_F(RaftTest, PreCampaignWhileLeader) {
  testCampaignWhileLeader(true);
}

TEST_F(RaftTest, CommitAfterRemoveNode) {
  TestCommitAfterRemoveNode();
}

TEST_F(RaftTest, LeaderTransferToUpToDateNode) {
  TestLeaderTransferToUpToDateNode();
}

TEST_F(RaftTest, LeaderTransferToUpToDateNodeFromFollower) {
  TestLeaderTransferToUpToDateNodeFromFollower();
}

TEST_F(RaftTest, LeaderTransferWithCheckQuorum) {
  TestLeaderTransferWithCheckQuorum();
}

TEST_F(RaftTest, LeaderTransferToSlowFollower) {
  TestLeaderTransferToSlowFollower();
}

TEST_F(RaftTest, LeaderTransferAfterSnapshot) {
  TestLeaderTransferAfterSnapshot();
}

TEST_F(RaftTest, LeaderTransferToSelf) {
  TestLeaderTransferToSelf();
}

TEST_F(RaftTest, LeaderTransferToNonExistingNode) {
  TestLeaderTransferToNonExistingNode();
}

TEST_F(RaftTest, LeaderTransferTimeout) {
  TestLeaderTransferTimeout();
}

TEST_F(RaftTest, LeaderTransferIgnoreProposal) {
  TestLeaderTransferIgnoreProposal();
}

TEST_F(RaftTest, LeaderTransferReceiveHigherTermVote) {
  TestLeaderTransferReceiveHigherTermVote();
}

TEST_F(RaftTest, LeaderTransferRemoveNode) {
  TestLeaderTransferRemoveNode();
}

TEST_F(RaftTest, LeaderTransferBack) {
  TestLeaderTransferBack();
}

TEST_F(RaftTest, LeaderTransferSecondTransferToAnotherNode) {
  TestLeaderTransferSecondTransferToAnotherNode();
}

TEST_F(RaftTest, LeaderTransferSecondTransferToSameNode) {
  TestLeaderTransferSecondTransferToSameNode();
}

TEST_F(RaftTest, TransferNonMember) {
  TestTransferNonMember();
}

TEST_F(RaftTest, NodeWithSmallerTermCanCompleteElection) {
  TestNodeWithSmallerTermCanCompleteElection();
}

TEST_F(RaftTest, PreVoteWithSplitVote) {
  TestPreVoteWithSplitVote();
}

TEST_F(RaftTest, PreVoteMigrationCanCompleteElection) {
  TestPreVoteMigrationCanCompleteElection();
}

TEST_F(RaftTest, PreVoteMigrationWithFreeStuckPreCandidate) {
  TestPreVoteMigrationWithFreeStuckPreCandidate();
}

}  // namespace yaraft
