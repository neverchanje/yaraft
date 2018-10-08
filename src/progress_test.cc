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

#include <yaraft/progress.h>

#include "test_utils.h"

namespace yaraft {

class ProgressTest : public BaseTest {
 public:
  void TestProgressBecomeProbe() {
    uint64_t match = 1;
    struct TestData {
      std::shared_ptr<Progress> p;

      uint64_t wnext;
    } tests[] = {
        {
            Progress().State(Progress::kStateReplicate).Match(match).Next(5).Ins(256).Sptr(),
            2,
        },
        {
            // snapshot finish
            Progress().State(Progress::kStateSnapshot).Match(match).Next(5).PendingSnapshot(10).Ins(256).Sptr(),
            11,
        },

        {
            // snapshot failure
            Progress().State(Progress::kStateSnapshot).Match(match).Next(5).PendingSnapshot(0).Ins(256).Sptr(),
            2,
        },
    };

    for (auto t : tests) {
      t.p->becomeProbe();

      ASSERT_EQ(t.p->state_, Progress::kStateProbe);
      ASSERT_EQ(t.p->match_, 1);
      ASSERT_EQ(t.p->next_, t.wnext);
    }
  }

  void TestProgressBecomeReplicate() {
    auto p = Progress().State(Progress::kStateProbe).Match(1).Next(5).Ins(256).Sptr();
    p->becomeReplicate();

    ASSERT_EQ(p->state_, Progress::kStateReplicate);
    ASSERT_EQ(p->match_, 1);
    ASSERT_EQ(p->next_, p->match_ + 1);
  }

  void TestProgressBecomeSnapshot() {
    auto p = Progress().State(Progress::kStateProbe).Match(1).Next(5).Ins(256).Sptr();
    p->becomeSnapshot(10);

    ASSERT_EQ(p->state_, Progress::kStateSnapshot);
    ASSERT_EQ(p->match_, 1);
    ASSERT_EQ(p->pendingSnapshot_, 10);
  }

  void TestProgressUpdate() {
    uint64_t prevM = 3, prevN = 5;
    struct TestData {
      uint64_t update;

      uint64_t wm;
      uint64_t wn;
      bool wok;
    } tests[] = {
        {prevM - 1, prevM, prevN, false},         // do not decrease match, next
        {prevM, prevM, prevN, false},             // do not decrease next
        {prevM + 1, prevM + 1, prevN, true},      // increase match, do not decrease next
        {prevM + 2, prevM + 2, prevN + 1, true},  // increase match, next
    };
    for (auto tt : tests) {
      auto p = Progress().Match(prevM).Next(prevN).Sptr();
      bool ok = p->maybeUpdate(tt.update);

      ASSERT_EQ(ok, tt.wok);
      ASSERT_EQ(p->match_, tt.wm);
      ASSERT_EQ(p->next_, tt.wn);
    }
  }

  void TestProgressMaybeDecr() {
    struct TestData {
      Progress::StateType state;
      uint64_t match;
      uint64_t next;
      uint64_t rejected;
      uint64_t last;

      bool w;
      uint64_t wn;
    } tests[] = {
        // state replicate and rejected is not greater than match
        {Progress::kStateReplicate, 5, 10, 5, 5, false, 10},

        // state replicate and rejected is not greater than match
        {Progress::kStateReplicate, 5, 10, 4, 4, false, 10},

        // state replicate and rejected is greater than match
        // directly decrease to match+1
        {Progress::kStateReplicate, 5, 10, 9, 9, true, 6},

        // next-1 != rejected is always false
        {Progress::kStateProbe, 0, 0, 0, 0, false, 0},

        // next-1 != rejected is always false
        {Progress::kStateProbe, 0, 10, 5, 5, false, 10},

        // next>1 = decremented by 1
        {Progress::kStateProbe, 0, 10, 9, 9, true, 9},

        // next>1 = decremented by 1
        {Progress::kStateProbe, 0, 2, 1, 1, true, 1},

        // next<=1 = reset to 1
        {Progress::kStateProbe, 0, 1, 0, 0, true, 1},

        // decrease to min(rejected, last+1)
        {Progress::kStateProbe, 0, 10, 9, 2, true, 3},

        // rejected < 1, reset to 1
        {Progress::kStateProbe, 0, 10, 9, 0, true, 1},
    };

    for (auto t : tests) {
      Progress p;
      p.State(t.state).Match(t.match).Next(t.next);

      ASSERT_EQ(p.maybeDecrTo(t.rejected, t.last), t.w);
      ASSERT_EQ(p.match_, t.match);
      ASSERT_EQ(p.next_, t.wn);
    }
  }

  void TestProgressIsPaused() {
    struct TestData {
      Progress::StateType state;
      bool paused;

      bool w;
    } tests[] = {
        {Progress::kStateProbe, false, false},
        {Progress::kStateProbe, true, true},
        {Progress::kStateReplicate, false, false},
        {Progress::kStateReplicate, true, false},
        {Progress::kStateSnapshot, false, true},
        {Progress::kStateSnapshot, true, true},
    };

    for (auto t : tests) {
      Progress p;
      p.State(t.state).Paused(t.paused).Ins(256);
      ASSERT_EQ(p.IsPaused(), t.w);
    }
  }

  // TestProgressResume ensures that progress.maybeUpdate and progress.maybeDecrTo
  // will reset progress.paused.
  void TestProgressResume() {
    Progress p;
    p.Next(2).Paused(true);
    p.maybeDecrTo(1, 1);
    ASSERT_EQ(p.paused_, false);
    p.paused_ = true;
    p.maybeUpdate(2);
    ASSERT_EQ(p.paused_, false);
  }

  // TestProgressResumeByHeartbeatResp ensures raft.heartbeat reset progress.paused by heartbeat response.
  void TestProgressResumeByHeartbeatResp() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 5, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->prs_[2]->paused_ = true;

    r->Step(idl::Message().from(1).to(1).type(idl::MsgBeat));
    ASSERT_EQ(r->prs_[2]->paused_, true);

    r->prs_[2]->becomeReplicate();
    r->Step(idl::Message().from(2).to(1).type(idl::MsgHeartbeatResp));
    ASSERT_EQ(r->prs_[2]->paused_, false);
  }

  void TestProgressPaused() {
    RaftUPtr r(newTestRaft(1, {1, 2}, 5, 1, new MemoryStorage));
    r->becomeCandidate();
    r->becomeLeader();
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));
    r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data("somedata")}));

    auto ms = r->readMessages();
    ASSERT_EQ(ms.size(), 1);
  }

  void TestProgressFlowControl() {
    auto c = newTestConfig(1, {1, 2}, 5, 1, new MemoryStorage);
    c->maxInflightMsgs = 3;
    c->maxSizePerMsg = 2048;
    RaftUPtr r(Raft::New(c));
    r->becomeCandidate();
    r->becomeLeader();

    // Throw away all the messages relating to the initial election.
    r->readMessages();

    // While node 2 is in probe state, propose a bunch of entries.
    r->prs_[2]->becomeProbe();
    std::string blob(1000, 'a');
    for (int i = 0; i < 10; i++) {
      r->Step(idl::Message().from(1).to(1).type(idl::MsgProp).entries({idl::Entry().data(blob)}));
    }

    auto ms = r->readMessages();
    // First append has two entries: the empty entry to confirm the
    // election, and the first proposal (only one proposal gets sent
    // because we're in probe state).
    ASSERT_EQ(ms.size(), 1);
    ASSERT_EQ(ms[0].type(), idl::MsgApp);
    ASSERT_EQ(ms[0].entries().size(), 2);
    ASSERT_EQ(ms[0].entries()[0].data().size(), 0);
    ASSERT_EQ(ms[0].entries()[1].data().size(), 1000);

    // When this append is acked, we change to replicate state and can
    // send multiple messages at once.
    r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(ms[0].entries()[1].index()));
    ms = r->readMessages();
    ASSERT_EQ(ms.size(), 3);
    for (const auto &m : ms) {
      ASSERT_EQ(ms[0].type(), idl::MsgApp);
      ASSERT_EQ(ms[0].entries().size(), 2);
    }

    // Ack all three of those messages together and get the last two
    // messages (containing three entries).
    r->Step(idl::Message().from(2).to(1).type(idl::MsgAppResp).index(ms[2].entries()[1].index()));
    ms = r->readMessages();
    ASSERT_EQ(ms.size(), 2);
    for (const auto &m : ms) {
      ASSERT_EQ(ms[0].type(), idl::MsgApp);
    }
    ASSERT_EQ(ms[0].entries().size(), 2);
    ASSERT_EQ(ms[1].entries().size(), 1);
  }
};

TEST_F(ProgressTest, BecomeProbe) {
  TestProgressBecomeProbe();
}

TEST_F(ProgressTest, BecomeReplicate) {
  TestProgressBecomeReplicate();
}

TEST_F(ProgressTest, BecomeSnapshot) {
  TestProgressBecomeSnapshot();
}

TEST_F(ProgressTest, Update) {
  TestProgressUpdate();
}

TEST_F(ProgressTest, MaybeDecrTo) {
  TestProgressMaybeDecr();
}

TEST_F(ProgressTest, IsPaused) {
  TestProgressIsPaused();
}

TEST_F(ProgressTest, Resume) {
  TestProgressResume();
}

TEST_F(ProgressTest, ResumeByHeartbeatResp) {
  TestProgressResumeByHeartbeatResp();
}

TEST_F(ProgressTest, Paused) {
  TestProgressPaused();
}

TEST_F(ProgressTest, FlowControl) {
  TestProgressFlowControl();
}

}  // namespace yaraft
