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

#include "raft.h"
#include "test_utils.h"

namespace yaraft {

static auto testingSnap = idl::Snapshot().metadata_index(11)  // magic number
                              .metadata_term(11)              // magic number
                              .metadata_conf_state(
                                  idl::ConfState().nodes({1, 2}));

class RaftSnapTest : public BaseTest {
 public:
  static void TestSendingSnapshotSetPendingSnapshot() {
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1}, 10, 1, storage));
    r->restore(testingSnap);

    r->becomeCandidate();
    r->becomeLeader();

    // force set the next of node 2, so that
    // node 2 needs a snapshot
    r->prs_[2]->Next(r->raftLog_->firstIndex());

    r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(r->prs_[2]->next_ - 1).reject(true));
    ASSERT_EQ(r->prs_[2]->pendingSnapshot_, 11);
  }

  static void TestPendingSnapshotPauseReplication() {
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->restore(testingSnap);

    r->becomeCandidate();
    r->becomeLeader();

    r->prs_[2]->becomeSnapshot(11);

    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).term(1).entries({idl::Entry().data("some data")}));
    auto msgs = r->readMessages();
    ASSERT_EQ(msgs.size(), 0);
  }

  static void TestSnapshotFailure() {
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->restore(testingSnap);

    r->becomeCandidate();
    r->becomeLeader();

    r->prs_[2]->next_ = 1;
    r->prs_[2]->becomeSnapshot(11);

    r->Step(idl::Message().from(2).to(1).type(idl::MsgSnapStatus).reject(true));
    ASSERT_EQ(r->prs_[2]->pendingSnapshot_, 0);
    ASSERT_EQ(r->prs_[2]->next_, 1);
    ASSERT_EQ(r->prs_[2]->paused_, true);
  }

  static void TestSnapshotSucceed() {
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->restore(testingSnap);

    r->becomeCandidate();
    r->becomeLeader();

    r->prs_[2]->next_ = 1;
    r->prs_[2]->becomeSnapshot(11);

    r->Step(idl::Message().from(2).to(1).type(idl::MsgSnapStatus).reject(false));
    ASSERT_EQ(r->prs_[2]->pendingSnapshot_, 0);
    ASSERT_EQ(r->prs_[2]->next_, 12);
    ASSERT_EQ(r->prs_[2]->paused_, true);
  }

  static void TestSnapshotAbort() {
    auto storage = new MemoryStorage;
    RaftUPtr r(newTestRaft(1, {1, 2}, 10, 1, storage));
    r->restore(testingSnap);

    r->becomeCandidate();
    r->becomeLeader();

    r->prs_[2]->next_ = 1;
    r->prs_[2]->becomeSnapshot(11);

    // A successful msgAppResp that has a higher/equal index than the
    // pending snapshot should abort the pending snapshot.
    r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(11));
    ASSERT_EQ(r->prs_[2]->pendingSnapshot_, 0);
    ASSERT_EQ(r->prs_[2]->next_, 12);
  }
};

TEST_F(RaftSnapTest, SendingSnapshotSetPendingSnapshot) {
  TestSendingSnapshotSetPendingSnapshot();
}

TEST_F(RaftSnapTest, PendingSnapshotPauseReplication) {
  TestPendingSnapshotPauseReplication();
}

TEST_F(RaftSnapTest, SnapshotFailure) {
  TestSnapshotFailure();
}

TEST_F(RaftSnapTest, SnapshotSucceed) {
  TestSnapshotSucceed();
}

TEST_F(RaftSnapTest, SnapshotAbort) {
  TestSnapshotAbort();
}

}  // namespace yaraft
