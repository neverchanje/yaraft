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

#include "memory_storage.h"
#include "raft.h"
#include "test_utils.h"

#include <gtest/gtest.h>

namespace yaraft {

class RaftPaperTest {
 public:
  // TestUpdateTermFromMessage tests that if one server’s current term is
  // smaller than the other’s, then it updates its current term to the larger
  // value. If a candidate or leader discovers that its term is out of date,
  // it immediately reverts to follower state.
  // Reference: section 5.1
  static void TestUpdateTermFromMessage(Raft::StateRole role) {
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
    switch (role) {
      case Raft::kLeader:
        r->becomeCandidate();
        r->becomeLeader();
        break;
      case Raft::kCandidate:
        r->becomeCandidate();
        break;
      case Raft::kFollower:
        break;
    }

    r->Step(PBMessage().Term(2).Type(pb::MsgApp).v);
    ASSERT_EQ(r->currentTerm_, 2);
    ASSERT_EQ(r->role_, Raft::kFollower);
  }

  // TestRejectStaleTermMessage tests that if a server receives a request with
  // a stale term number, it rejects the request.
  // Our implementation ignores the request instead.
  // Reference: section 5.1
  static void TestRejectStaleTermMessage() {
    // This is already tested by Raft.StepIgnoreOldTermMsg
  }

  // TestStartAsFollower tests that when servers start up, they begin as followers.
  // Reference: section 5.2
  static void TestStartAsFollower() {
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, 1, new MemoryStorage()));
    ASSERT_EQ(r->role_, Raft::kFollower);
  }

  // TestLeaderBcastBeat tests that if the leader receives a heartbeat tick,
  // it will send a msgApp with m.Index = 0, m.LogTerm=0 and empty entries as
  // heartbeat to all followers.
  // Reference: section 5.2
  static void TestLeaderBcastBeat() {
    int heartbeatInterval = 1;
    RaftUPtr r(newTestRaft(1, {1, 2, 3}, 10, heartbeatInterval, new MemoryStorage()));
    r->becomeCandidate();
    r->becomeLeader();

    for (int i = 0; i < heartbeatInterval; i++) {
      r->_tick();
    }

    ASSERT_EQ(r->mails_.size(), 2);

    std::unordered_set<std::string> s1;
    std::for_each(r->mails_.begin(), r->mails_.end(),
                  [&](const pb::Message& m) { s1.insert(DumpPB(m)); });

    std::unordered_set<std::string> s2;
    s2.insert(DumpPB(PBMessage().From(1).To(2).Term(1).Commit(0).Type(pb::MsgHeartbeat).v));
    s2.insert(DumpPB(PBMessage().From(1).To(3).Term(1).Commit(0).Type(pb::MsgHeartbeat).v));

    ASSERT_EQ(s1, s2);
  }
};

}  // namespace yaraft

using namespace yaraft;

TEST(Raft, FollowerUpdateTermFromMessage) {
  RaftPaperTest::TestUpdateTermFromMessage(Raft::kFollower);
}

TEST(Raft, CandidateUpdateTermFromMessage) {
  RaftPaperTest::TestUpdateTermFromMessage(Raft::kCandidate);
}

TEST(Raft, LeaderUpdateTermFromMessage) {
  RaftPaperTest::TestUpdateTermFromMessage(Raft::kLeader);
}

TEST(Raft, StartAsFollower) {
  RaftPaperTest::TestStartAsFollower();
}

TEST(Raft, LeaderBcastBeat) {
  RaftPaperTest::TestLeaderBcastBeat();
}
