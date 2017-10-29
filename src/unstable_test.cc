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

#include "test_utils.h"
#include "unstable.h"

using namespace yaraft;

class UnstableTest : public BaseTest {};

TEST_F(UnstableTest, TruncateAndAppend) {
  struct TestData {
    std::vector<pb::Entry> entries;
    uint64_t offset;
    pb::Snapshot* snap;
    std::vector<pb::Entry> toAppend;

    uint64_t woffset;
    std::vector<pb::Entry> wentries;
  } tests[] = {
      {
          // append to the end
          {pbEntry(5, 1)},
          5,
          nullptr,
          {pbEntry(6, 1), pbEntry(7, 1)},
          5,
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
      },
      {
          // replace the unstable entries
          {pbEntry(5, 1)},
          5,
          nullptr,
          {pbEntry(5, 2), pbEntry(6, 2)},
          5,
          {pbEntry(5, 2), pbEntry(6, 2)},
      },
      {
          {pbEntry(5, 1)},
          5,
          nullptr,
          {pbEntry(4, 2), pbEntry(5, 2), pbEntry(6, 2)},
          4,
          {pbEntry(4, 2), pbEntry(5, 2), pbEntry(6, 2)},
      },

      /// truncate the existing entries and append
      {
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
          5,
          nullptr,
          {pbEntry(6, 2)},
          5,
          {pbEntry(5, 1), pbEntry(6, 2)},
      },
      {
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
          5,
          nullptr,
          {pbEntry(7, 2), pbEntry(8, 2)},
          5,
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 2), pbEntry(8, 2)},
      },
  };

  for (auto t : tests) {
    Unstable u;
    u.entries = t.entries;
    u.offset = t.offset;
    u.snapshot.reset(t.snap);

    auto msg = PBMessage().Entries(t.toAppend).v;
    u.TruncateAndAppend(msg.mutable_entries()->begin(), msg.mutable_entries()->end());
    ASSERT_EQ(u.offset, t.woffset);

    EntryVec_ASSERT_EQ(u.entries, t.wentries);
  }
}

TEST_F(UnstableTest, Restore) {
  Unstable u;
  u.entries = {PBEntry().Index(5).Term(1).v};
  u.offset = 6;

  auto snap = PBSnapshot().MetaIndex(4).MetaTerm(1).v;
  u.Restore(snap);

  ASSERT_EQ(u.offset, 5);
  ASSERT_EQ(u.entries.size(), 0);
}

TEST_F(UnstableTest, MaybeTerm) {
  struct TestData {
    std::vector<pb::Entry> entries;
    uint64_t offset;
    pb::Snapshot* snap;
    uint64_t index;

    uint64_t wterm;
  } tests[] = {
      // term from entries
      {
          {PBEntry().Index(5).Term(1).v}, 5, nullptr, 5, 1,
      },
      {
          {PBEntry().Index(5).Term(1).v}, 5, nullptr, 6, 0,
      },
      {
          {PBEntry().Index(5).Term(1).v}, 5, nullptr, 4, 0,
      },
      {
          {PBEntry().Index(5).Term(1).v},
          5,
          new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v),
          5,
          1,
      },
      {
          {PBEntry().Index(5).Term(1).v},
          5,
          new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v),
          6,
          0,
      },
      // term from snapshot
      {
          {PBEntry().Index(5).Term(1).v},
          5,
          new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v),
          4,
          1,
      },
      {
          {PBEntry().Index(5).Term(1).v},
          5,
          new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v),
          3,
          0,
      },
      {
          {}, 5, new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v), 5, 0,
      },
      {
          {}, 5, new pb::Snapshot(PBSnapshot().MetaIndex(4).MetaTerm(1).v), 4, 1,
      },
      {
          {}, 0, nullptr, 5, 0,
      },
  };

  for (auto t : tests) {
    Unstable u;
    u.offset = t.offset;
    u.entries = t.entries;
    u.snapshot.reset(t.snap);

    ASSERT_EQ(u.MaybeTerm(t.index), t.wterm);
  }
}

TEST_F(UnstableTest, CopyTo) {
  struct TestData {
    EntryVec ents;
    uint64_t lo, hi;
    int maxSize;

    EntryVec wents;
  } tests[] = {
      // no space enough
      {{pbEntry(2, 2)}, 1, 3, 0, {}},

      // space only for 1 entry
      {{pbEntry(2, 2)}, 1, 3, pbEntry(1, 1).ByteSize(), {pbEntry(1, 1)}},

      // space enough for 2 entries
      {{pbEntry(2, 2)}, 1, 3, pbEntry(1, 1).ByteSize() + pbEntry(2, 2).ByteSize(), {pbEntry(1, 1)}},

      {{pbEntry(2, 2), pbEntry(3, 3)},
       2,
       4,
       static_cast<int>(noLimit),
       {pbEntry(2, 2), pbEntry(3, 3)}},

      {{pbEntry(2, 2), pbEntry(3, 3)}, 3, 4, static_cast<int>(noLimit), {pbEntry(3, 3)}},
  };

  for (auto t : tests) {
    Unstable u;
    u.entries = {pbEntry(1, 1)};
    std::copy(t.ents.begin(), t.ents.end(), std::back_inserter(u.entries));
    u.offset = 1;

    EntryVec result;
    u.CopyTo(result, t.lo, t.hi, static_cast<uint64_t>(t.maxSize));
  }
}