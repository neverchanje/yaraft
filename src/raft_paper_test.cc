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