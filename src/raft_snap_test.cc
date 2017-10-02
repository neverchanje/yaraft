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
  static void TestSendingSnapshotSetPendingSnapshot() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));
    r->restore(snap);

    r->becomeCandidate();
    r->becomeLeader();

    // force set the next of node 1, so that node 1 needs a snapshot
    // nextIndex[2] = 12
    r->prs_[2].NextIndex(r->log_->FirstIndex());

    r->Step(PBMessage()
                .From(2)
                .To(1)
                .Type(pb::MsgAppResp)
                .Index(r->prs_[2].NextIndex() - 1)
                .Term(1)
                .Reject()
                .v);

    ASSERT_EQ(r->prs_[2].PendingSnapshot(), 11);
  }

  static void TestPendingSnapshotPauseReplication() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));

    r->becomeCandidate();
    r->becomeLeader();

    r->prs_[2].BecomeSnapshot(11);

    r->Step(PBMessage()
                .From(1)
                .To(1)
                .Type(pb::MsgProp)
                .Term(1)
                .Entries({PBEntry().Data("some data").v})
                .v);
    ASSERT_EQ(r->mails_.size(), 0);
  }

  static void TestSnapshotFailure() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));

    r->becomeCandidate();
    r->becomeLeader();

    Progress& pr = r->prs_[2];
    pr.BecomeSnapshot(11);

    r->Step(PBMessage().From(2).To(1).Term(1).Type(pb::MsgSnapStatus).Reject().v);
    ASSERT_EQ(pr.PendingSnapshot(), 0);
    ASSERT_EQ(pr.NextIndex(), 1);
    ASSERT_TRUE(pr.IsPaused());
  }

  static void TestSnapshotSucceed() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));

    r->becomeCandidate();
    r->becomeLeader();

    Progress& pr = r->prs_[2];
    pr.BecomeSnapshot(11);

    r->Step(PBMessage().From(2).To(1).Term(1).Type(pb::MsgSnapStatus).v);
    ASSERT_EQ(pr.PendingSnapshot(), 0);
    ASSERT_EQ(pr.NextIndex(), 12);
    ASSERT_TRUE(pr.IsPaused());
  }

  static void TestSnapshotAbort() {
    auto snap = PBSnapshot().MetaIndex(11).MetaTerm(11).MetaConfState({1, 2}).v;
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));

    r->becomeCandidate();
    r->becomeLeader();

    Progress& pr = r->prs_[2];
    pr.BecomeSnapshot(11);

    // A successful msgAppResp that has a higher/equal index than the
    // pending snapshot should abort the pending snapshot.
    r->Step(PBMessage().From(2).To(1).Index(11).Term(r->Term()).Type(pb::MsgAppResp).v);

    ASSERT_EQ(pr.State(), Progress::StateProbe);
    ASSERT_EQ(pr.NextIndex(), 12);
    ASSERT_EQ(pr.PendingSnapshot(), 0);
  }
};

}  // namespace yaraft

using namespace yaraft;

TEST_F(RaftTest, SendingSnapshotSetPendingSnapshot) {
  RaftTest::TestSendingSnapshotSetPendingSnapshot();
}

TEST_F(RaftTest, PendingSnapshotPauseReplication) {
  RaftTest::TestPendingSnapshotPauseReplication();
}

TEST_F(RaftTest, SnapshotFailure) {
  RaftTest::TestSnapshotFailure();
}

TEST_F(RaftTest, SnapshotSucceed) {
  RaftTest::TestSnapshotSucceed();
}

TEST_F(RaftTest, SnapshotAbort) {
  RaftTest::TestSnapshotAbort();
}