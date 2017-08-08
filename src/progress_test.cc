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

#include "progress.h"

#include <gtest/gtest.h>

using namespace yaraft;

TEST(Progress, MaybeDecrTo) {
  struct TestData {
    Progress::StateType state;
    uint64_t match;
    uint64_t next;
    uint64_t rejected;
    uint64_t last;

    bool w;
    uint64_t wn;
  } tests[] = {
      {
          // state replicate and rejected is not greater than match
          Progress::StateReplicate, 5, 10, 5, 5, false, 10,
      },
      {
          // state replicate and rejected is not greater than match
          Progress::StateReplicate, 5, 10, 4, 4, false, 10,
      },
      {
          // state replicate and rejected is greater than match
          // directly decrease to match+1
          Progress::StateReplicate, 5, 10, 9, 9, true, 6,
      },
      {
          // next-1 != rejected is always false
          Progress::StateProbe, 0, 0, 0, 0, false, 0,
      },
      {
          // next-1 != rejected is always false
          Progress::StateProbe, 0, 10, 5, 5, false, 10,
      },
      {
          // next>1 = decremented by 1
          Progress::StateProbe, 0, 10, 9, 9, true, 9,
      },
      {
          // next>1 = decremented by 1
          Progress::StateProbe, 0, 2, 1, 1, true, 1,
      },
      {
          // next<=1 = reset to 1
          Progress::StateProbe, 0, 1, 0, 0, true, 1,
      },
      {
          // decrease to min(rejected, last+1)
          Progress::StateProbe, 0, 10, 9, 2, true, 3,
      },
      {
          // rejected < 1, reset to 1
          Progress::StateProbe, 0, 10, 9, 0, true, 1,
      },
  };

  for (auto t : tests) {
    Progress p;
    p.State(t.state).MatchIndex(t.match).NextIndex(t.next);

    ASSERT_EQ(p.MaybeDecrTo(t.rejected, t.last), t.w) << p;
    ASSERT_EQ(p.MatchIndex(), t.match) << p;
    ASSERT_EQ(p.NextIndex(), t.wn) << p;
  }
}

TEST(Progress, BecomeProbe) {
  struct TestData {
    Progress p;

    uint64_t wnext;
  } tests[] = {
      {
          Progress().State(Progress::StateReplicate).NextIndex(5).MatchIndex(1), 2,
      },

      {
          // snapshot finish
          Progress().State(Progress::StateSnapshot).NextIndex(5).PendingSnapshot(10), 11,
      },

      {
          // snapshot failure
          Progress().State(Progress::StateSnapshot).NextIndex(5).MatchIndex(1).PendingSnapshot(0),
          2,
      },
  };

  for (auto t : tests) {
    t.p.BecomeProbe();

    ASSERT_EQ(t.p.State(), Progress::StateProbe);
    ASSERT_EQ(t.p.NextIndex(), t.wnext);
  }
}

TEST(Progress, BecomeReplicate) {
  auto p = Progress().State(Progress::StateProbe).MatchIndex(1).NextIndex(5);
  p.BecomeReplicate();

  ASSERT_EQ(p.State(), Progress::StateReplicate);
  ASSERT_EQ(p.NextIndex(), 2);
  ASSERT_EQ(p.MatchIndex(), 1);
}

TEST(Progress, BecomeSnapshot) {
  auto p = Progress().State(Progress::StateProbe).MatchIndex(1).NextIndex(5);
  p.BecomeSnapshot(10);

  ASSERT_EQ(p.State(), Progress::StateSnapshot);
  ASSERT_EQ(p.MatchIndex(), 1);
  ASSERT_EQ(p.PendingSnapshot(), 10);
}