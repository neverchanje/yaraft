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

#include <unordered_set>

#include "raft.h"
#include "test_utils.h"

namespace yaraft {

class RaftPaperTest : public BaseTest {
 public:
  void TestFollowerUpdateTermFromMessage() {
    testUpdateTermFromMessage(StateType::kFollower);
  }
  void TestCandidateUpdateTermFromMessage() {
    testUpdateTermFromMessage(StateType::kCandidate);
  }
  void TestLeaderUpdateTermFromMessage() {
    testUpdateTermFromMessage(StateType::kLeader);
  }

  // TestUpdateTermFromMessage tests that if one server’s current term is
  // smaller than the other’s, then it updates its current term to the larger
  // value. If a candidate or leader discovers that its term is out of date,
  // it immediately reverts to follower state.
  // Reference: section 5.1
  void testUpdateTermFromMessage(StateType role) {
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
    switch (role) {
      case StateType::kFollower:
        r->becomeFollower(1, 2);
        break;
      case StateType::kCandidate:
        r->becomeCandidate();
        break;
      case StateType::kLeader:
        r->becomeCandidate();
        r->becomeLeader();
        break;
      default:
        break;
    }

    r->Step(idl::Message().term(2).type(idl::MsgApp));

    ASSERT_EQ(r->term_, 2);
    ASSERT_EQ(r->state_, StateType::kFollower);
  }

  // TestRejectStaleTermMessage tests that if a server receives a request with
  // a stale term number, it rejects the request.
  // Our implementation ignores the request instead.
  // Reference: section 5.1
  void TestRejectStaleTermMessage() {
    // This is already tested by Raft.StepIgnoreOldTermMsg
  }

  // TestStartAsFollower tests that when servers start up, they begin as followers.
  // Reference: section 5.2
  void TestStartAsFollower() {
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
    ASSERT_EQ(r->state_, StateType::kFollower);
  }

  // TestLeaderBcastBeat tests that if the leader receives a heartbeat tick,
  // it will send a msgApp with m.Index = 0, m.LogTerm=0 and empty entries as
  // heartbeat to all followers.
  // Reference: section 5.2
  void TestLeaderBcastBeat() {
    int heartbeatInterval = 1;
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, heartbeatInterval, new MemoryStorage()));
    r->becomeCandidate();
    r->becomeLeader();
    for (uint64_t i = 0; i < 10; i++) {
      idl::EntryVec v{newEntry(i + 1, 0)};
      r->appendEntry(v);
    }

    for (int i = 0; i < heartbeatInterval; i++) {
      r->tick();
    }

    auto msgs = r->readMessages();
    std::sort(msgs.begin(), msgs.end());
    auto wmsgs = std::vector<idl::Message>{
        idl::Message().from(1).to(2).term(1).type(idl::MsgHeartbeat).commit(0),
        idl::Message().from(1).to(3).term(1).type(idl::MsgHeartbeat).commit(0),
    };
    ASSERT_EQ(msgs.size(), 2);
    ASSERT_EQ(msgs, wmsgs);
  }

  void TestFollowerStartElection() {
    testNonleaderStartElection(StateType::kFollower);
  }
  void TestCandidateStartNewElection() {
    testNonleaderStartElection(StateType::kCandidate);
  }

  // testNonleaderStartElection tests that if a follower receives no communication
  // over election timeout, it begins an election to choose a new leader. It
  // increments its current term and transitions to candidate state. It then
  // votes for itself and issues RequestVote RPCs in parallel to each of the
  // other servers in the cluster.
  // Reference: section 5.2
  // Also if a candidate fails to obtain a majority, it will time out and
  // start a new election by incrementing its term and initiating another
  // round of RequestVote RPCs.
  // Reference: section 5.2
  void testNonleaderStartElection(StateType role) {
    int electionTimeout = 10;
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, electionTimeout, 1, new MemoryStorage()));
    if (role == StateType::kFollower) {
      r->becomeFollower(1, 2);
    } else if (role == StateType::kCandidate) {
      r->becomeCandidate();
    }

    for (int i = 1; i < 2 * electionTimeout; i++) {
      r->tick();
    }

    ASSERT_EQ(r->term_, 2);
    ASSERT_EQ(r->state_, StateType::kCandidate);

    // vote for self
    ASSERT_EQ(r->vote_, r->id_);
    ASSERT_TRUE(r->votes_[r->id_]);

    auto msgs = r->readMessages();
    std::sort(msgs.begin(), msgs.end());
    auto wmsgs = std::vector<idl::Message>{
        idl::Message().from(1).to(2).term(2).logterm(0).index(0).type(idl::MsgVote),
        idl::Message().from(1).to(3).term(2).logterm(0).index(0).type(idl::MsgVote),
    };
    ASSERT_EQ(msgs, wmsgs);
  }

  // TestLeaderElectionInOneRoundRPC tests all cases that may happen in
  // leader election during one round of RequestVote RPC:
  // a) it wins the election
  // b) another server establishes itself as leader
  // c) a period of time goes by with no winner
  // Reference: section 5.2
  void TestLeaderElectionInOneRoundRPC() {
    struct TestData {
      size_t size;
      std::map<uint64_t, bool> votes;
      StateType wstate;
    } tests[] = {
        // win the election when receiving votes from a majority of the servers
        {1, {}, StateType::kLeader},
        {3, {{2, true}, {3, true}}, StateType::kLeader},
        {3, {{2, true}}, StateType::kLeader},
        {5, {{2, true}, {3, true}, {4, true}, {5, true}}, StateType::kLeader},
        {5, {{2, true}, {3, true}, {4, true}}, StateType::kLeader},
        {5, {{2, true}, {3, true}}, StateType::kLeader},

        // return to follower state if it receives vote denial from a majority
        {3, {{2, false}, {3, false}}, StateType::kFollower},
        {5, {{2, false}, {3, false}, {4, false}, {5, false}}, StateType::kFollower},
        {5, {{2, true}, {3, false}, {4, false}, {5, false}}, StateType::kFollower},

        // stay in candidate if it does not obtain the majority
        {3, {}, StateType::kCandidate},
        {5, {{2, true}}, StateType::kCandidate},
        {5, {{2, false}, {3, false}}, StateType::kCandidate},
        {5, {}, StateType::kCandidate},
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, idsBySize(tt.size), 10, 1, new MemoryStorage()));

      r->Step(idl::Message().from(1).to(1).type(idl::MsgHup));
      for (auto p : tt.votes) {
        uint64_t id = p.first;
        bool vote = p.second;
        r->Step(idl::Message().from(id).to(1).term(r->term_).type(idl::MsgVoteResp).reject(!vote));
      }

      ASSERT_EQ(r->state_, tt.wstate);
      ASSERT_EQ(r->term_, 1);
    }
  }

  // TestFollowerVote tests that each follower will vote for at most one
  // candidate in a given term, on a first-come-first-served basis.
  // Reference: section 5.2
  void TestFollowerVote() {
    struct TestData {
      uint64_t vote;
      uint64_t nvote;
      bool wreject;
    } tests[] = {
        {kNone, 1, false},
        {kNone, 2, false},
        {1, 1, false},
        {2, 2, false},
        {1, 2, true},
        {2, 1, true},
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
      r->loadState(idl::HardState().term(1).vote(tt.vote));

      r->Step(idl::Message().from(tt.nvote).to(1).term(1).type(idl::MsgVote));

      auto msgs = r->readMessages();
      auto wmsgs = std::vector<idl::Message>{
          idl::Message().from(1).to(tt.nvote).term(1).type(idl::MsgVoteResp).reject(tt.wreject),
      };
      for (auto& m : msgs) {
        m.reject(m.reject());  // set reject field
      }
      ASSERT_EQ(msgs, wmsgs);
    }
  }

  // TestCandidateFallback tests that while waiting for votes,
  // if a candidate receives an AppendEntries RPC from another server claiming
  // to be leader whose term is at least as large as the candidate's current term,
  // it recognizes the leader as legitimate and returns to follower state.
  // Reference: section 5.2
  static void TestCandidateFallback() {
    std::vector<idl::Message> tests = {
        idl::Message().from(2).to(1).term(1).type(idl::MsgApp),
        idl::Message().from(2).to(1).term(2).type(idl::MsgApp),
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
      r->Step(idl::Message().from(1).to(1).type(idl::MsgHup));
      ASSERT_EQ(r->state_, StateType::kCandidate);

      r->Step(tt);

      ASSERT_EQ(r->state_, StateType::kFollower);
      ASSERT_EQ(r->term_, tt.term());
    }
  }

  // TestLeaderStartReplication tests that when receiving client proposals,
  // the leader appends the proposal to its log as a new entry, then issues
  // AppendEntries RPCs in parallel to each of the other servers to replicate
  // the entry. Also, when sending an AppendEntries RPC, the leader includes
  // the index and term of the entry in its log that immediately precedes
  // the new entries.
  // Also, it writes the new entry into stable storage.
  // Reference: section 5.3
  void TestLeaderStartReplication() {
    auto s = new MemoryStorage();
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, s));
    r->becomeCandidate();
    r->becomeLeader();
    commitNoopEntry(*r, *s);
    uint64_t li = r->raftLog_->lastIndex();

    auto ents = idl::EntryVec{idl::Entry().data("some data")};
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries(ents));

    ASSERT_EQ(r->raftLog_->lastIndex(), li + 1);
    ASSERT_EQ(r->raftLog_->committed_, li);
    auto msgs = r->readMessages();
    std::sort(msgs.begin(), msgs.end());
    idl::EntryVec wents({idl::Entry().term(1).index(li + 1).data("some data")});

    std::vector<idl::Message> wmsgs{
        idl::Message().from(1).to(2).term(1).type(idl::MsgApp).index(li).logterm(1).entries(wents).commit(li),
        idl::Message().from(1).to(3).term(1).type(idl::MsgApp).index(li).logterm(1).entries(wents).commit(li),
    };
    ASSERT_EQ(msgs, wmsgs);
    EntryVec_ASSERT_EQ(r->raftLog_->unstableEntries(), wents);
  }

  // TestLeaderCommitEntry tests that when the entry has been safely replicated,
  // the leader gives out the applied entries, which can be applied to its state
  // machine.
  // Also, the leader keeps track of the highest index it knows to be committed,
  // and it includes that index in future AppendEntries RPCs so that the other
  // servers eventually find out.
  // Reference: section 5.3
  void TestLeaderCommitEntry() {
    int heartbeatTimeout = 3;
    auto s = new MemoryStorage();
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, heartbeatTimeout, s));
    r->becomeCandidate();
    r->becomeLeader();
    commitNoopEntry(*r, *s);
    uint64_t li = r->raftLog_->lastIndex();
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries(idl::EntryVec{idl::Entry().data("some data")}));

    for (auto& m : r->readMessages()) {
      r->Step(acceptAndReply(m));
    }

    ASSERT_EQ(r->raftLog_->committed_, li + 1);
    auto wents = idl::EntryVec{idl::Entry().index(li + 1).term(1).data("some data")};
    EntryVec_ASSERT_EQ(r->raftLog_->nextEnts(), wents);
    auto msgs = r->readMessages();
    std::sort(msgs.begin(), msgs.end());
    for (int i = 0; i < msgs.size(); i++) {
      auto& m = msgs[i];
      ASSERT_EQ(m.to(), i + 2);
      ASSERT_EQ(m.type(), idl::MsgApp);
      ASSERT_EQ(m.commit(), li + 1);
    }
  }

  // TestLeaderAcknowledgeCommit tests that a log entry is committed once the
  // leader that created the entry has replicated it on a majority of the
  // servers.
  // Reference: section 5.3
  void TestLeaderAcknowledgeCommit() {
    struct TestData {
      size_t size;
      std::map<uint64_t, bool> acceptors;
      bool wack;
    } tests[] = {
        {1, {}, true},
        {3, {}, false},
        {3, {{2, true}}, true},
        {3, {{2, true}, {3, true}}, true},
        {5, {}, false},
        {5, {{2, true}}, false},
        {5, {{2, true}, {3, true}}, true},
        {5, {{2, true}, {3, true}, {4, true}}, true},
        {5, {{2, true}, {3, true}, {4, true}, {5, true}}, true},
    };

    for (auto t : tests) {
      auto s = new MemoryStorage;
      RaftUPtr r(newTestRaft(1, idsBySize(t.size), 10, 1, s));
      r->becomeCandidate();
      r->becomeLeader();
      commitNoopEntry(*r, *s);
      uint64_t li = r->raftLog_->lastIndex();
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries(idl::EntryVec{idl::Entry().data("some data")}));

      for (auto& m : r->readMessages()) {
        if (t.acceptors.find(m.to()) != t.acceptors.end()) {
          r->Step(acceptAndReply(m));
        }
      }

      ASSERT_EQ(r->raftLog_->committed_ > li, t.wack);
    }
  }

  // TestLeaderCommitPrecedingEntries tests that when leader commits a log entry,
  // it also commits all preceding entries in the leader’s log, including
  // entries created by previous leaders.
  // Also, it applies the entry to its local state machine (in log order).
  // Reference: section 5.3
  void TestLeaderCommitPrecedingEntries() {
    idl::EntryVec tests[] = {
        {},
        {{newEntry(1, 2)}},
        {{newEntry(1, 1), newEntry(2, 2)}},
        {{newEntry(1, 1)}},
    };

    for (auto tt : tests) {
      auto s = new MemoryStorage;
      s->Append(tt);
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, s));
      r->loadState(idl::HardState().term(2));
      r->becomeCandidate();
      r->becomeLeader();

      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).term(r->term_).entries(idl::EntryVec{idl::Entry().data("some data")}));

      for (auto& m : r->readMessages()) {
        r->Step(acceptAndReply(m));
      }

      uint64_t li = tt.size();

      idl::EntryVec v{idl::Entry().term(3).index(li + 1), idl::Entry().term(3).index(li + 2).data("some data")};
      tt.Append(v);
      EntryVec_ASSERT_EQ(r->raftLog_->nextEnts(), tt);
    }
  }

  // TestFollowerCommitEntry tests that once a follower learns that a log entry
  // is committed, it applies the entry to its local state machine (in log order).
  // Reference: section 5.3
  void TestFollowerCommitEntry() {
    struct TestData {
      idl::EntryVec ents;
      uint64_t commit;
    } tests[] = {
        {
            idl::EntryVec{idl::Entry().term(1).index(1).data("some data")},
            1,
        },
        {
            idl::EntryVec{
                idl::Entry().term(1).index(1).data("some data"),
                idl::Entry().term(1).index(2).data("some data2"),
            },
            2,
        },
        {
            idl::EntryVec{
                idl::Entry().term(1).index(1).data("some data2"),
                idl::Entry().term(1).index(2).data("some data"),
            },
            2,
        },
        {
            idl::EntryVec{
                idl::Entry().term(1).index(1).data("some data"),
                idl::Entry().term(1).index(2).data("some data2"),
            },
            1,
        },
    };

    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage));
      r->becomeFollower(1, 2);

      r->Step(idl::Message().from(2).to(1).type(idl::MsgApp).term(1).entries(tt.ents).commit(tt.commit));

      ASSERT_EQ(r->raftLog_->committed_, tt.commit);
      tt.ents.SliceTo(tt.commit);
      auto wents = tt.ents;
      EntryVec_ASSERT_EQ(r->raftLog_->nextEnts(), wents);
    }
  }

  // TestFollowerCheckMsgApp tests that if the follower does not find an
  // entry in its log with the same index and term as the one in AppendEntries RPC,
  // then it refuses the new entries. Otherwise it replies that it accepts the
  // append entries.
  // Reference: section 5.3
  void TestFollowerCheckMsgApp() {
    idl::EntryVec ents{idl::Entry().term(1).index(1), idl::Entry().term(2).index(2)};
    struct TestData {
      uint64_t term;
      uint64_t index;

      uint64_t windex;
      bool wreject;
      uint64_t wrejecthint;
    } tests[] = {
        // match with committed entries
        {0, 0, 1, false, 0},
        {ents[0].term(), ents[0].index(), 1, false, 0},
        // match with uncommitted entries
        {ents[1].term(), ents[1].index(), 2, false, 0},

        // unmatch with existing entry
        {ents[0].term(), ents[1].index(), ents[1].index(), true, 2},
        // unexisting entry
        {ents[1].term() + 1, ents[1].index() + 1, ents[1].index() + 1, true, 2},
    };

    for (auto tt : tests) {
      auto s = new MemoryStorage;
      s->Append(ents);
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, s));
      r->loadState(idl::HardState().commit(1));
      r->becomeFollower(2, 2);

      r->Step(idl::Message().from(2).to(1).type(idl::MsgApp).term(2).logterm(tt.term).index(tt.index));

      auto msgs = r->readMessages();
      std::vector<idl::Message> wmsgs{
          idl::Message().from(1).to(2).type(idl::MsgAppResp).term(2).index(tt.windex).reject(tt.wreject).rejecthint(tt.wrejecthint),
      };

      ASSERT_EQ(msgs.size(), 1);
      msgs[0].reject(msgs[0].reject()).rejecthint(msgs[0].rejecthint());
      ASSERT_EQ(msgs, wmsgs);
    }
  }

  // TestFollowerAppendEntries tests that when AppendEntries RPC is valid,
  // the follower will delete the existing conflict entry and all that follow
  // it,
  // and append any new entries not already in the log.
  // Also, it writes the new entry into stable storage.
  // Reference: section 5.3
  void TestFollowerAppendEntries() {
    struct TestData {
      uint64_t index, term;
      idl::EntryVec ents;
      idl::EntryVec wents;
      idl::EntryVec wunstable;
    } tests[] = {
        {
            2,
            2,
            {newEntry(3, 3)},
            {newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)},
            {newEntry(3, 3)},
        },
        {
            1,
            1,
            {newEntry(2, 3), newEntry(3, 4)},
            {newEntry(1, 1), newEntry(2, 3), newEntry(3, 4)},
            {newEntry(2, 3), newEntry(3, 4)},
        },
        {0, 0, {newEntry(1, 1)}, {newEntry(1, 1), newEntry(2, 2)}},
        {0, 0, {newEntry(1, 3)}, {newEntry(1, 3)}, {newEntry(1, 3)}},
    };

    for (auto t : tests) {
      idl::EntryVec ents{newEntry(1, 1), newEntry(2, 2)};
      auto s = new MemoryStorage;
      s->Append(ents);

      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, s));
      r->becomeFollower(2, 2);

      r->Step(idl::Message().from(2).to(1).type(idl::MsgApp).term(2).logterm(t.term).index(t.index).entries(t.ents));

      EntryVec_ASSERT_EQ(r->raftLog_->allEntries(), t.wents);
      EntryVec_ASSERT_EQ(r->raftLog_->unstableEntries(), t.wunstable);
    }
  }

  // TestLeaderSyncFollowerLog tests that the leader could bring a follower's log
  // into consistency with its own.
  // Reference: section 5.3, figure 7
  static void TestLeaderSyncFollowerLog() {
    idl::EntryVec ents{
        {},
        newEntry(1, 1),
        newEntry(2, 1),
        newEntry(3, 1),
        newEntry(4, 4),
        newEntry(5, 4),
        newEntry(6, 5),
        newEntry(7, 5),
        newEntry(8, 6),
        newEntry(9, 6),
        newEntry(10, 6),
    };
    uint64_t term = 8;
    idl::EntryVec tests[] = {
        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 4), newEntry(5, 4), newEntry(6, 5), newEntry(7, 5), newEntry(8, 6), newEntry(9, 6)},

        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 4)},

        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 4), newEntry(5, 4), newEntry(6, 5), newEntry(7, 5), newEntry(8, 6), newEntry(9, 6), newEntry(10, 6), newEntry(11, 6)},

        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 4), newEntry(5, 4), newEntry(6, 5), newEntry(7, 5), newEntry(8, 6), newEntry(9, 6), newEntry(10, 6), newEntry(11, 7), newEntry(12, 7)},

        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 4), newEntry(5, 4), newEntry(6, 4), newEntry(7, 4)},

        {{}, newEntry(1, 1), newEntry(2, 1), newEntry(3, 1), newEntry(4, 2), newEntry(5, 2), newEntry(6, 2), newEntry(7, 3), newEntry(8, 3), newEntry(9, 3), newEntry(10, 3), newEntry(11, 3)},
    };

    for (auto tt : tests) {
      auto leadStorage = new MemoryStorage;
      leadStorage->Append(ents);
      auto lead = newTestRaft(1, {1, 2, 3}, 10, 1, leadStorage);
      lead->loadState(idl::HardState().commit(lead->raftLog_->lastIndex()).term(8));

      auto followerStorage = new MemoryStorage;
      followerStorage->Append(tt);
      auto follower = newTestRaft(2, {1, 2, 3}, 10, 1, followerStorage);
      follower->loadState(idl::HardState().term(term - 1));

      // It is necessary to have a three-node cluster.
      // The second may have more up-to-date log than the first one, so the
      // first node needs the vote from the third node to become the leader.
      Network n({lead, follower, nopStepper});
      n.Send({idl::Message().from(1).to(1).type(idl::MsgHup)});
      // The election occurs in the term after the one we loaded with
      // lead.loadState above.
      n.Send({idl::Message().from(3).to(1).type(idl::MsgVoteResp).term(term + 1)});

      n.Send({idl::Message().from(1).to(1).type(idl::MsgProp).entries({{}})});

      ASSERT_EQ(lead->raftLog_->ltoa(), follower->raftLog_->ltoa());
    }
  }

  // TestVoteRequest tests that the vote request includes information about the candidate’s log
  // and are sent to all of the other nodes.
  // Reference: section 5.4.1
  static void TestVoteRequest() {
    struct TestData {
      idl::EntryVec ents;
      uint64_t wterm;
    } tests[] = {
        {{newEntry(1, 1)}, 2},
        {{newEntry(1, 1), newEntry(2, 2)}, 3},
    };
    for (auto tt : tests) {
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
      r->Step(idl::Message().from(2).to(1).type(idl::MsgApp).term(tt.wterm - 1).logterm(0).index(0).entries(tt.ents));
      r->readMessages();

      for (int i = 1; i < r->electionTimeout_ * 2; i++) {
        r->tickElection();
      }

      auto msgs = r->readMessages();
      std::sort(msgs.begin(), msgs.end());
      ASSERT_EQ(msgs.size(), 2);
      for (int i = 0; i < msgs.size(); i++) {
        auto m = msgs[i];

        ASSERT_EQ(m.type(), idl::MsgVote);
        ASSERT_EQ(m.term(), tt.wterm);
        ASSERT_EQ(m.to(), i + 2);
        uint64_t windex = tt.ents[tt.ents.size() - 1].index(), wlogterm = tt.ents[tt.ents.size() - 1].term();
        ASSERT_EQ(m.index(), windex);
        ASSERT_EQ(m.logterm(), wlogterm);
      }
    }
  }

  // TestVoter tests the voter denies its vote if its own log is more up-to-date
  // than that of the candidate.
  // Reference: section 5.4.1
  static void TestVoter() {
    struct TestData {
      idl::EntryVec ents;
      uint64_t logterm;
      uint64_t index;

      bool wreject;
    } tests[] = {
        // same logterm
        {{newEntry(1, 1)}, 1, 1, false},
        {{newEntry(1, 1)}, 1, 2, false},
        {{newEntry(1, 1), newEntry(2, 1)}, 1, 1, true},

        // candidate higher logterm
        {{newEntry(1, 1)}, 2, 1, false},
        {{newEntry(1, 1)}, 2, 2, false},
        {{newEntry(1, 1), newEntry(2, 1)}, 2, 1, false},

        // voter higher logterm
        {{newEntry(1, 2)}, 1, 1, true},
        {{newEntry(1, 2)}, 1, 2, true},
        {{newEntry(1, 1), newEntry(2, 2)}, 1, 1, true},
    };

    for (auto tt : tests) {
      auto storage = new MemoryStorage();
      storage->Append(tt.ents);
      RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, storage));
      r->Step(idl::Message().from(2).to(1).type(idl::MsgVote).term(3).logterm(tt.logterm).index(tt.index));

      auto msgs = r->readMessages();
      ASSERT_EQ(msgs.size(), 1);
      auto& m = msgs[0];
      ASSERT_EQ(m.type(), idl::MsgVoteResp);
      ASSERT_EQ(m.reject(), tt.wreject);
    }
  }

  // TestLeaderOnlyCommitsLogFromCurrentTerm tests that only log entries from the leader’s
  // current term are committed by counting replicas.
  // Reference: section 5.4.2
  static void TestLeaderOnlyCommitsLogFromCurrentTerm() {
    idl::EntryVec ents{newEntry(1, 1), newEntry(2, 2)};
    struct TestData {
      uint64_t index;
      uint64_t wcommit;
    } tests[] = {
        // do not commit log entries in previous terms
        {1, 0},
        {2, 0},
        // commit log in current term
        {3, 3},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage();
      storage->Append(ents);
      RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
      r->loadState(idl::HardState().term(2));
      // become leader at term 3
      r->becomeCandidate();
      r->becomeLeader();
      r->readMessages();
      // propose a entry to current term
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({{}}));

      r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).term(r->term_).index(t.index));
      ASSERT_EQ(r->raftLog_->committed_, t.wcommit);
    }
  }

  void commitNoopEntry(Raft& r, MemoryStorage& s) {
    ASSERT_EQ(r.state_, StateType::kLeader);

    r.bcastAppend();
    // simulate the response of MsgApp
    auto msgs = r.readMessages();
    for (auto& m : msgs) {
      ASSERT_EQ(m.type(), idl::MsgApp);
      ASSERT_EQ(m.entries().size(), 1);
      ASSERT_EQ(m.entries()[0].data(), "");
      r.Step(acceptAndReply(m));
    }
    // ignore further messages to refresh followers' commit index
    r.readMessages();
    s.Append(r.raftLog_->unstableEntries());
    r.raftLog_->appliedTo(r.raftLog_->committed_);
    r.raftLog_->stableTo(r.raftLog_->lastIndex(), r.raftLog_->lastTerm());
  }

  idl::Message acceptAndReply(idl::Message& m) {
    EXPECT_EQ(idl::MessageType(m.type()), idl::MsgApp);
    return idl::Message()
        .from(m.to())
        .term(m.term())
        .type(idl::MsgAppResp)
        .index(m.index() + m.entries().size());
  }
};

TEST_F(RaftPaperTest, FollowerUpdateTermFromMessage) {
  TestFollowerUpdateTermFromMessage();
}

TEST_F(RaftPaperTest, CandidateUpdateTermFromMessage) {
  TestCandidateUpdateTermFromMessage();
}

TEST_F(RaftPaperTest, LeaderUpdateTermFromMessage) {
  TestLeaderUpdateTermFromMessage();
}

TEST_F(RaftPaperTest, StartAsFollower) {
  TestStartAsFollower();
}

TEST_F(RaftPaperTest, LeaderBcastBeat) {
  TestLeaderBcastBeat();
}

TEST_F(RaftPaperTest, FollowerStartElection) {
  TestFollowerStartElection();
}

TEST_F(RaftPaperTest, CandidateStartNewElection) {
  TestCandidateStartNewElection();
}

TEST_F(RaftPaperTest, LeaderElectionInOneRoundRPC) {
  TestLeaderElectionInOneRoundRPC();
}

TEST_F(RaftPaperTest, FollowerVote) {
  TestFollowerVote();
}

TEST_F(RaftPaperTest, CandidateFallback) {
  TestCandidateFallback();
}

TEST_F(RaftPaperTest, LeaderStartReplication) {
  TestLeaderStartReplication();
}

TEST_F(RaftPaperTest, LeaderCommitEntry) {
  TestLeaderCommitEntry();
}

TEST_F(RaftPaperTest, LeaderAcknowledgeCommit) {
  TestLeaderAcknowledgeCommit();
}

TEST_F(RaftPaperTest, LeaderCommitPrecedingEntries) {
  TestLeaderCommitPrecedingEntries();
}

TEST_F(RaftPaperTest, FollowerCommitEntry) {
  TestFollowerCommitEntry();
}

TEST_F(RaftPaperTest, FollowerCheckMsgApp) {
  TestFollowerCheckMsgApp();
}

TEST_F(RaftPaperTest, FollowerAppendEntries) {
  TestFollowerAppendEntries();
}

TEST_F(RaftPaperTest, LeaderSyncFollowerLog) {
  TestLeaderSyncFollowerLog();
}

TEST_F(RaftPaperTest, VoteRequest) {
  TestVoteRequest();
}

TEST_F(RaftPaperTest, Voter) {
  TestVoter();
}

TEST_F(RaftPaperTest, LeaderOnlyCommitsLogFromCurrentTerm) {
  TestLeaderOnlyCommitsLogFromCurrentTerm();
}

}  // namespace yaraft
