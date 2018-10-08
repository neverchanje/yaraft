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

#include "unstable.h"
#include "test_utils.h"

namespace yaraft {

class UnstableTest : public BaseTest {
 public:
  void TestUnstableMaybeFirstIndex() {
    struct TestData {
      idl::EntryVec entries;
      uint64_t offset;
      std::unique_ptr<idl::Snapshot> snap;

      bool wok;
      uint64_t windex;
    } tests[] = {
        // no snapshot
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            false,
            0,
        },
        {
            {},
            0,
            nullptr,
            false,
            0,
        },
        // has snapshot
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            true,
            5,
        },
        {
            {},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            true,
            5,
        },
    };

    for (auto &tt : tests) {
      Unstable u;
      u.entries = tt.entries;
      u.offset = tt.offset;
      u.snapshot = std::move(tt.snap);

      auto p = u.maybeFirstIndex();
      uint64_t index = p.first;
      bool ok = p.second;

      ASSERT_EQ(ok, tt.wok);
      ASSERT_EQ(index, tt.windex);
    }
  }

  void TestUnstableMaybeLastIndex() {
    struct TestData {
      idl::EntryVec entries;
      uint64_t offset;
      std::unique_ptr<idl::Snapshot> snap;

      bool wok;
      uint64_t windex;
    } tests[] = {
        // last in entries
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            true,
            5,
        },
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            true,
            5,
        },
        // last in snapshot
        {
            {},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            true,
            4,
        },
        // empty unstable
        {
            {},
            0,
            nullptr,
            false,
            0,
        },
    };

    for (auto &tt : tests) {
      Unstable u;
      u.entries = tt.entries;
      u.offset = tt.offset;
      u.snapshot = std::move(tt.snap);

      auto p = u.maybeLastIndex();
      uint64_t index = p.first;
      bool ok = p.second;

      ASSERT_EQ(ok, tt.wok);
      ASSERT_EQ(index, tt.windex);
    }
  }

  void TestUnstableMaybeTerm() {
    struct TestData {
      idl::EntryVec entries;
      uint64_t offset;
      std::unique_ptr<idl::Snapshot> snap;
      uint64_t index;

      bool wok;
      uint64_t wterm;
    } tests[] = {
        // term from entries
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            5,
            true,
            1,
        },
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            6,
            false,
            0,
        },
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            4,
            false,
            0,
        },
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            4,
            true,
            1,
        },
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            6,
            false,
            0,
        },
        // term from snapshot
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            4,
            true,
            1,
        },
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            3,
            false,
            0,
        },
        {
            {},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            5,
            false,
            0,
        },
        {
            {},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            4,
            true,
            1,
        },
        {
            {},
            0,
            nullptr,
            5,
            false,
            0,
        },
    };

    for (auto &tt : tests) {
      Unstable u;
      u.entries = tt.entries;
      u.offset = tt.offset;
      u.snapshot = std::move(tt.snap);

      auto p = u.maybeTerm(tt.index);
      uint64_t term = p.first;
      bool ok = p.second;

      ASSERT_EQ(ok, tt.wok);
      ASSERT_EQ(term, tt.wterm);
    }
  }

  void TestUnstableRestore() {
    Unstable u;
    u.entries = {newEntry(5, 1)};
    u.offset = 5;
    u.snapshot = make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1));

    u.restore(idl::Snapshot().metadata_index(6).metadata_term(2));
    ASSERT_EQ(u.offset, 7);
    ASSERT_EQ(u.entries.size(), 0);
    ASSERT_EQ(u.snapshot->metadata_index(), 6);
    ASSERT_EQ(u.snapshot->metadata_term(), 2);
  }

  void TestUnstableStableTo() {
    struct TestData {
      idl::EntryVec entries;
      uint64_t offset;
      std::unique_ptr<idl::Snapshot> snap;
      uint64_t index, term;

      uint64_t woffset;
      int wlen;
    } tests[] = {
        {{}, 0, nullptr, 5, 1, 0, 0},

        {{newEntry(5, 1)}, 5, nullptr, 5, 1,  // stable to the first entry
         6,
         0},

        {{newEntry(5, 1), newEntry(6, 1)}, 5, nullptr, 5, 1,  // stable to the first entry
         6,
         1},

        {{newEntry(6, 2)}, 6, nullptr, 6, 1,  // stable to the first entry and term mismatch
         6,
         1},

        {{newEntry(5, 1)}, 5, nullptr, 4, 1,  // stable to old entry
         5,
         1},

        {{newEntry(5, 1)}, 5, nullptr, 4, 2,  // stable to old entry
         5,
         1},

        // with snapshot
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            5,
            1,  // stable to the first entry
            6,
            0,
        },
        {
            {newEntry(5, 1), newEntry(6, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            5,
            1,  // stable to the first entry
            6,
            1,
        },
        {
            {newEntry(6, 2)},
            6,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(5).metadata_term(1)),
            6,
            1,  // stable to the first entry and term mismatch
            6,
            1,
        },
        {
            {newEntry(5, 1)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(1)),
            4,
            1,  // stable to snapshot
            5,
            1,
        },
        {
            {newEntry(5, 2)},
            5,
            make_unique<idl::Snapshot>(idl::Snapshot().metadata_index(4).metadata_term(2)),
            4,
            1,  // stable to old entry
            5,
            1,
        },
    };

    for (auto &tt : tests) {
      Unstable u;
      u.entries = tt.entries;
      u.offset = tt.offset;
      u.snapshot = std::move(tt.snap);

      u.stableTo(tt.index, tt.term);
      ASSERT_EQ(u.offset, tt.woffset);
      ASSERT_EQ(u.entries.size(), tt.wlen);
    }
  }

  void TestUnstableTruncateAndAppend() {
    struct TestData {
      idl::EntryVec entries;
      uint64_t offset;
      std::unique_ptr<idl::Snapshot> snap;
      idl::EntryVec toAppend;

      uint64_t woffset;
      idl::EntryVec wentries;
    } tests[] = {
        // append to the end
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            {newEntry(6, 1), newEntry(7, 1)},
            5,
            {newEntry(5, 1), newEntry(6, 1), newEntry(7, 1)},
        },
        // replace the unstable entries
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            {newEntry(5, 2), newEntry(6, 2)},
            5,
            {newEntry(5, 2), newEntry(6, 2)},
        },
        {
            {newEntry(5, 1)},
            5,
            nullptr,
            {newEntry(4, 2), newEntry(5, 2), newEntry(6, 2)},
            4,
            {newEntry(4, 2), newEntry(5, 2), newEntry(6, 2)},
        },
        // truncate the existing entries and append
        {
            {newEntry(5, 1), newEntry(6, 1), newEntry(7, 1)},
            5,
            nullptr,
            {newEntry(6, 2)},
            5,
            {newEntry(5, 1), newEntry(6, 2)},
        },
        {
            {newEntry(5, 1), newEntry(6, 1), newEntry(7, 1)},
            5,
            nullptr,
            {newEntry(7, 2), newEntry(8, 2)},
            5,
            {newEntry(5, 1), newEntry(6, 1), newEntry(7, 2), newEntry(8, 2)},
        },
    };

    for (auto &tt : tests) {
      Unstable u;
      u.entries = tt.entries;
      u.offset = tt.offset;
      u.snapshot = std::move(tt.snap);

      u.truncateAndAppend(tt.toAppend);
      ASSERT_EQ(u.offset, tt.woffset);
      EntryVec_ASSERT_EQ(u.entries, tt.wentries);
    }
  }
};

TEST_F(UnstableTest, MaybeFirstIndex) {
  TestUnstableMaybeFirstIndex();
}

TEST_F(UnstableTest, MaybeLastIndex) {
  TestUnstableMaybeLastIndex();
}

TEST_F(UnstableTest, MaybeTerm) {
  TestUnstableMaybeTerm();
}

TEST_F(UnstableTest, Restore) {
  TestUnstableRestore();
}

TEST_F(UnstableTest, StableTo) {
  TestUnstableStableTo();
}

TEST_F(UnstableTest, TruncateAndAppend) {
  TestUnstableTruncateAndAppend();
}

}  // namespace yaraft
