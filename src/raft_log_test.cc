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
#include "raft_log.h"
#include "test_utils.h"

namespace yaraft {

uint64_t mustTerm(const RaftLog &log, uint64_t index) {
  auto s = log.term(index);
  EXPECT_TRUE(s.is_ok());
  return s.get_value();
}

class RaftLogTest : public BaseTest {
 public:
  void TestFindConflict() {
    auto previousEnts = idl::EntryVec({newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)});
    struct TestData {
      idl::EntryVec ents;
      uint64_t wconflict;
    } tests[] = {
        // no conflict, empty ent
        {{}, 0},
        // no conflict
        {{newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)}, 0},
        {{newEntry(2, 2), newEntry(3, 3)}, 0},
        {{newEntry(3, 3)}, 0},
        // no conflict, but has new entries
        {{newEntry(1, 1), newEntry(2, 2), newEntry(3, 3), newEntry(4, 4), newEntry(5, 4)}, 4},
        {{newEntry(2, 2), newEntry(3, 3), newEntry(4, 4), newEntry(5, 4)}, 4},
        {{newEntry(3, 3), newEntry(4, 4), newEntry(5, 4)}, 4},
        {{newEntry(4, 4), newEntry(5, 4)}, 4},
        // conflicts with existing entries
        {{newEntry(1, 4), newEntry(2, 4)}, 1},
        {{newEntry(2, 1), newEntry(3, 4), newEntry(4, 4)}, 2},
        {{newEntry(3, 1), newEntry(4, 2), newEntry(5, 4), newEntry(6, 4)}, 3},
    };

    for (auto tt : tests) {
      RaftLog raftLog(new MemoryStorage);
      raftLog.append(previousEnts);

      auto gconflict = raftLog.findConflict(tt.ents);
      ASSERT_EQ(gconflict, tt.wconflict);
    }
  }

  void TestIsUpToDate() {
    auto previousEnts = idl::EntryVec({newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)});

    RaftLog raftLog(new MemoryStorage);
    raftLog.append(previousEnts);

    struct TestData {
      uint64_t lastIndex;
      uint64_t term;
      bool wUpToDate;
    } tests[] = {
        // greater term, ignore lastIndex
        {raftLog.lastIndex() - 1, 4, true},
        {raftLog.lastIndex(), 4, true},
        {raftLog.lastIndex() + 1, 4, true},
        // smaller term, ignore lastIndex
        {raftLog.lastIndex() - 1, 2, false},
        {raftLog.lastIndex(), 2, false},
        {raftLog.lastIndex() + 1, 2, false},
        // equal term, equal or lager lastIndex wins
        {raftLog.lastIndex() - 1, 3, false},
        {raftLog.lastIndex(), 3, true},
        {raftLog.lastIndex() + 1, 3, true},
    };

    for (auto tt : tests) {
      auto gUpToDate = raftLog.isUpToDate(tt.lastIndex, tt.term);
      ASSERT_EQ(gUpToDate, tt.wUpToDate);
    }
  }

  void TestAppend() {
    idl::EntryVec previousEnts({newEntry(1, 1), newEntry(2, 2)});
    struct TestData {
      idl::EntryVec ents;
      uint64_t windex;
      idl::EntryVec wents;
      uint64_t wunstable;
    } tests[] = {
        {
            {},
            2,
            {newEntry(1, 1), newEntry(2, 2)},
            3,
        },
        {
            {newEntry(3, 2)},
            3,
            {newEntry(1, 1), newEntry(2, 2), newEntry(3, 2)},
            3,
        },
        // conflicts with index 1
        {
            {newEntry(1, 2)},
            1,
            {newEntry(1, 2)},
            1,
        },
        // conflicts with index 2
        {
            {newEntry(2, 3), newEntry(3, 3)},
            3,
            {newEntry(1, 1), newEntry(2, 3), newEntry(3, 3)},
            2,
        },
    };

    for (auto tt : tests) {
      auto storage = new MemoryStorage;
      storage->Append(previousEnts);
      RaftLog raftLog(storage);

      uint64_t index = raftLog.append(tt.ents);
      ASSERT_EQ(index, tt.windex);
      auto errWithEntries = raftLog.entries(1, noLimit);
      ASSERT_TRUE(errWithEntries.is_ok());
      auto g = errWithEntries.get_value();
      EntryVec_ASSERT_EQ(g, tt.wents);
      ASSERT_EQ(raftLog.unstable_.offset, tt.wunstable);
    }
  }

  // TestLogMaybeAppend ensures:
  // If the given (index, term) matches with the existing log:
  //  1. If an existing entry conflicts with a new one (same index
  //  but different terms), delete the existing entry and all that
  //  follow it
  //  2.Append any new entries not already in the log
  // If the given (index, term) does not match with the existing log:
  //  return false
  void TestLogMaybeAppend() {
    auto previousEnts = idl::EntryVec({newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)});
    uint64_t lastindex = 3;
    uint64_t lastterm = 3;
    uint64_t commit = 1;

    struct TestData {
      uint64_t logTerm;
      uint64_t index;
      uint64_t committed;
      idl::EntryVec ents;

      uint64_t wlasti;
      bool wappend;
      uint64_t wcommit;
      bool wpanic;
    } tests[] = {
        // not match: term is different
        {lastterm - 1, lastindex, lastindex, {newEntry(lastindex + 1, 4)}, 0, false, commit, false},

        // not match: index out of bound
        {lastterm, lastindex + 1, lastindex, {newEntry(lastindex + 2, 4)}, 0, false, commit, false},

        // match with the last existing entry
        {lastterm, lastindex, lastindex, {}, lastindex, true, lastindex, false},

        {
            lastterm, lastindex, lastindex + 1, {}, lastindex, true, lastindex, false,
            // do not increase commit higher than lastnewi
        },
        {
            lastterm, lastindex, lastindex - 1, {}, lastindex, true, lastindex - 1, false
            // commit up to the commit in the message
        },
        {
            lastterm, lastindex, 0, {}, lastindex, true, commit, false
            // commit do not decrease
        },
        {
            0, 0, lastindex, {}, 0, true, commit, false
            // commit do not decrease
        },

        {lastterm, lastindex, lastindex, {newEntry(lastindex + 1, 4)}, lastindex + 1, true, lastindex, false},

        {lastterm, lastindex, lastindex + 1, {newEntry(lastindex + 1, 4)}, lastindex + 1, true, lastindex + 1, false},

        {
            lastterm, lastindex, lastindex + 2, {newEntry(lastindex + 1, 4)}, lastindex + 1, true, lastindex + 1, false
            // do not increase commit higher than lastnewi
        },

        {
            lastterm,
            lastindex,
            lastindex + 2,
            {newEntry(lastindex + 1, 4), newEntry(lastindex + 2, 4)},
            lastindex + 2,
            true,
            lastindex + 2,
            false,
        },

        // match with the the entry in the middle
        {lastterm - 1, lastindex - 1, lastindex, {newEntry(lastindex, 4)}, lastindex, true, lastindex, false},

        {lastterm - 2, lastindex - 2, lastindex, {newEntry(lastindex - 1, 4)}, lastindex - 1, true, lastindex - 1, false},

        {
            lastterm - 3, lastindex - 3, lastindex, {newEntry(lastindex - 2, 4)}, lastindex - 2, true, lastindex - 2, true
            // conflict with existing committed entry
        },

        {
            lastterm - 2,
            lastindex - 2,
            lastindex,
            {newEntry(lastindex - 1, 4), newEntry(lastindex, 4)},
            lastindex,
            true,
            lastindex,
            false,
        },
    };

    for (auto tt : tests) {
      RaftLog log(new MemoryStorage());
      log.append(previousEnts);
      log.committed_ = commit;

      try {
        auto p = log.maybeAppend(tt.index, tt.logTerm, tt.committed, tt.ents);
        uint64_t glasti = p.first;
        bool gappend = p.second;
        uint64_t gcommit = log.committed_;

        ASSERT_EQ(glasti, tt.wlasti);
        ASSERT_EQ(gappend, tt.wappend);
        ASSERT_EQ(gcommit, tt.wcommit);
        if (gappend && !tt.ents.empty()) {
          auto errWithEntries = log.slice(log.lastIndex() - tt.ents.size() + 1, log.lastIndex() + 1, noLimit);
          ASSERT_TRUE(errWithEntries.is_ok());
          auto gents = errWithEntries.get_value();
          EntryVec_ASSERT_EQ(gents, tt.ents);
        }
      } catch (RaftError &e) {
        ASSERT_TRUE(tt.wpanic);
      }
    }
  }

  // TestCompactionSideEffects ensures that all the log related functionality works correctly after
  // a compaction.
  void TestCompactionSideEffects() {
    // Populate the log with 1000 entries; 750 in stable storage and 250 in unstable.
    uint64_t lastIndex = 1000;
    uint64_t unstableIndex = 750;
    uint64_t lastTerm = lastIndex;

    auto storage = new MemoryStorage;
    for (uint64_t i = 1; i <= unstableIndex; i++) {
      idl::EntryVec v = {newEntry(i, i)};
      ASSERT_TRUE(storage->Append(v).is_ok());
    }
    RaftLog log(storage);
    for (uint64_t i = unstableIndex; i < lastIndex; i++) {
      idl::EntryVec v = {newEntry(i + 1, i + 1)};
      log.append(v);
    }

    bool ok = log.maybeCommit(lastIndex, lastTerm);
    ASSERT_TRUE(ok);
    log.appliedTo(log.committed_);

    uint64_t offset = 500;
    storage->Compact(offset);

    ASSERT_EQ(log.lastIndex(), lastIndex);

    for (uint64_t j = offset; j <= log.lastIndex(); j++) {
      ASSERT_EQ(mustTerm(log, j), j);
    }
    for (uint64_t j = offset; j <= log.lastIndex(); j++) {
      ASSERT_TRUE(log.matchTerm(j, j));
    }

    auto unstableEnts = log.unstableEntries();
    ASSERT_EQ(unstableEnts.size(), 250);
    ASSERT_EQ(unstableEnts[0].index(), 751);

    auto prev = log.lastIndex();
    idl::EntryVec v = {newEntry(log.lastIndex() + 1, log.lastIndex() + 1)};
    log.append(v);
    ASSERT_EQ(log.lastIndex(), prev + 1);

    auto errWithEnts = log.entries(log.lastIndex(), noLimit);
    ASSERT_TRUE(errWithEnts.is_ok());
    ASSERT_EQ(errWithEnts.get_value().size(), 1);
  }

  void TestHasNextEnts() {
    auto snap = idl::Snapshot().metadata_index(3).metadata_term(1);
    auto ents = idl::EntryVec{newEntry(4, 1), newEntry(5, 1), newEntry(6, 1)};

    struct TestData {
      uint64_t applied;
      bool hasNext;
    } tests[] = {
        {0, true},
        {3, true},
        {4, true},
        {5, false},
    };

    for (auto tt : tests) {
      auto storage = new MemoryStorage;
      storage->ApplySnapshot(snap);
      RaftLog log(storage);
      log.append(ents);
      log.maybeCommit(5, 1);
      log.appliedTo(tt.applied);

      bool hasNext = log.hasNextEnts();
      ASSERT_EQ(hasNext, tt.hasNext);
    }
  }

  void TestNextEnts() {
    auto snap = idl::Snapshot().metadata_index(3).metadata_term(1);
    auto ents = idl::EntryVec{newEntry(4, 1), newEntry(5, 1), newEntry(6, 1)};

    struct TestData {
      uint64_t applied;
      idl::EntrySpan wents;
    } tests[] = {
        {0, ents.View().SliceTo(2)},
        {3, ents.View().SliceTo(2)},
        {4, ents.View().Slice(1, 2)},
        {5, {}},
    };

    for (auto tt : tests) {
      auto storage = new MemoryStorage;
      storage->ApplySnapshot(snap);
      RaftLog log(storage);
      log.append(ents);
      log.maybeCommit(5, 1);
      log.appliedTo(tt.applied);

      auto nents = log.nextEnts();
      EntryVec_ASSERT_EQ(nents, tt.wents);
    }
  }

  // TestUnstableEnts ensures unstableEntries returns the unstable part of the
  // entries correctly.
  void TestUnstableEnts() {
    auto previousEnts = idl::EntryVec{newEntry(1, 1), newEntry(2, 2)};
    uint64_t commit = 2;
    struct TestData {
      uint64_t unstable;
      idl::EntryVec wents;
      bool wpanic;
    } tests[] = {
        {3, {}},
        {1, previousEnts},
    };
    for (int i = 0; i < 2; i++) {
      auto tt = tests[i];

      // append stable entries to storage
      auto storage = new MemoryStorage;
      storage->Append(previousEnts.View().SliceTo(tt.unstable - 1));

      // append unstable entries to raftlog
      RaftLog raftLog(storage);
      raftLog.append(previousEnts.View().SliceFrom(tt.unstable - 1));

      auto ents = idl::EntryVec(raftLog.unstableEntries());
      size_t l = ents.size();
      if (l > 0) {
        raftLog.stableTo(ents[l - 1].index(), ents[l - i].term());
      }
      EntryVec_ASSERT_EQ(ents, tt.wents);

      auto w = previousEnts[previousEnts.size() - 1].index() + 1;
      ASSERT_EQ(raftLog.unstable_.offset, w);
    }
  }

  void TestCommitTo() {
    auto previousEnts = idl::EntryVec{newEntry(1, 1), newEntry(2, 2), newEntry(3, 3)};
    uint64_t commit = 2;
    struct TestData {
      uint64_t commit;
      uint64_t wcommit;
      bool wpanic;
    } tests[] = {
        {3, 3, false},
        {1, 2, false},  // never decrease
        {4, 0, true},   // commit out of range -> panic
    };

    for (auto tt : tests) {
      try {
        RaftLog raftLog(new MemoryStorage);
        raftLog.append(previousEnts);
        raftLog.committed_ = commit;
        raftLog.commitTo(tt.commit);
        ASSERT_EQ(raftLog.committed_, tt.wcommit);
      } catch (RaftError &) {
        ASSERT_TRUE(tt.wpanic);
      }
    }
  }

  void TestStableTo() {
    struct TestData {
      uint64_t stablei;
      uint64_t stablet;
      uint64_t wunstable;
    } tests[] = {
        {1, 1, 2},
        {2, 2, 3},
        {2, 1, 1},  // bad term
        {3, 1, 1},  // bad index
    };

    for (auto tt : tests) {
      RaftLog l(new MemoryStorage);

      idl::EntryVec v{newEntry(1, 1), newEntry(2, 2)};
      l.append(v);
      l.stableTo(tt.stablei, tt.stablet);
      ASSERT_EQ(tt.wunstable, l.unstable_.offset);
    }
  }

  void TestStableToWithSnap() {
    uint64_t snapi = 5, snapt = 2;
    struct TestData {
      uint64_t stablei;
      uint64_t stablet;
      idl::EntryVec newEnts;
      uint64_t wunstable;
    } tests[] = {
        {snapi + 1, snapt, {}, snapi + 1},
        {snapi, snapt, {}, snapi + 1},
        {snapi - 1, snapt, {}, snapi + 1},

        {snapi + 1, snapt + 1, {}, snapi + 1},
        {snapi, snapt + 1, {}, snapi + 1},
        {snapi - 1, snapt + 1, {}, snapi + 1},

        {snapi + 1, snapt, {newEntry(snapi + 1, snapt)}, snapi + 2},
        {snapi, snapt, {newEntry(snapi + 1, snapt)}, snapi + 1},
        {snapi - 1, snapt, {newEntry(snapi + 1, snapt)}, snapi + 1},

        {snapi + 1, snapt + 1, {newEntry(snapi + 1, snapt)}, snapi + 1},
        {snapi, snapt + 1, {newEntry(snapi + 1, snapt)}, snapi + 1},
        {snapi - 1, snapt + 1, {newEntry(snapi + 1, snapt)}, snapi + 1},
    };

    for (auto tt : tests) {
      auto s = new MemoryStorage;
      s->ApplySnapshot(idl::Snapshot().metadata_index(snapi).metadata_term(snapt));
      RaftLog l(s);
      l.append(tt.newEnts);
      l.stableTo(tt.stablei, tt.stablet);
      ASSERT_EQ(tt.wunstable, l.unstable_.offset);
    }
  }

  void TestSlice() {
    uint64_t offset = 100;
    uint64_t num = 100;
    uint64_t last = offset + num;
    uint64_t half = offset + num / 2;
    auto halfe = newEntry(half, half);

    auto storage = new MemoryStorage;
    storage->ApplySnapshot(idl::Snapshot().metadata_index(offset));
    for (uint64_t i = 1; i < num / 2; i++) {
      idl::EntryVec v{newEntry(offset + i, offset + i)};
      storage->Append(v);
    }
    RaftLog l(storage);
    for (uint64_t i = num / 2; i < num; i++) {
      idl::EntryVec v{newEntry(offset + i, offset + i)};
      l.append(v);
    }

    struct TestData {
      uint64_t from, to, limit;

      idl::EntryVec w;
      bool wpanic;
    } tests[] = {
        // test no limit
        {offset - 1, offset + 1, noLimit, {}, false},
        {offset, offset + 1, noLimit, {}, false},
        {half - 1, half + 1, noLimit, idl::EntryVec{newEntry(half - 1, half - 1), newEntry(half, half)}, false},
        {half, half + 1, noLimit, idl::EntryVec{newEntry(half, half)}, false},
        {last - 1, last, noLimit, idl::EntryVec{newEntry(last - 1, last - 1)}, false},
        {last, last + 1, noLimit, {}, true},

        // test limit
        {half - 1, half + 1, 0, idl::EntryVec{newEntry(half - 1, half - 1)}, false},
        {half - 1, half + 1, halfe.ByteSize() + 1, idl::EntryVec{newEntry(half - 1, half - 1)}, false},
        {half - 2, half + 1, halfe.ByteSize() + 1, idl::EntryVec{newEntry(half - 2, half - 2)}, false},
        {half - 1, half + 1, halfe.ByteSize() * 2, idl::EntryVec{newEntry(half - 1, half - 1), newEntry(half, half)}, false},
        {half - 1, half + 2, halfe.ByteSize() * 3, idl::EntryVec{newEntry(half - 1, half - 1), newEntry(half, half), newEntry(half + 1, half + 1)}, false},
        {half, half + 2, halfe.ByteSize(), idl::EntryVec{newEntry(half, half)}, false},
        {half, half + 2, halfe.ByteSize() * 2, idl::EntryVec{newEntry(half, half), newEntry(half + 1, half + 1)}, false},
    };

    for (auto tt : tests) {
      try {
        auto errWithEnts = l.slice(tt.from, tt.to, tt.limit);
        if (tt.from <= offset) {
          ASSERT_FALSE(errWithEnts.is_ok());
          ASSERT_EQ(errWithEnts.get_error(), ErrCompacted);
        }
        if (tt.from > offset) {
          ASSERT_TRUE(errWithEnts.is_ok());
        }

        idl::EntryVec ents;
        if (errWithEnts.is_ok()) {
          ents = errWithEnts.get_value();
        }
        EntryVec_ASSERT_EQ(ents, tt.w);
      } catch (RaftError &e) {
        ASSERT_TRUE(tt.wpanic);
      }
    }
  }

  void TestCompaction() {
    struct TestData {
      uint64_t lastIndex;
      std::vector<uint64_t> compact;
      std::vector<int> wleft;
      bool wallow;
    } tests[] = {
        // out of upper bound
        {1000, {1001}, {-1}, false},
        {1000, {300, 500, 800, 900}, {700, 500, 200, 100}, true},
        // out of lower bound
        {1000, {300, 299}, {700, -1}, false},
    };

    for (auto t : tests) {
      auto storage = new MemoryStorage;
      for (uint64_t i = 1; i <= t.lastIndex; i++) {
        idl::EntryVec v{newEntry(i, 0)};
        storage->Append(v);
      }

      RaftLog log(storage);
      log.maybeCommit(t.lastIndex, 0);
      log.appliedTo(log.committed_);

      try {
        for (int i = 0; i < t.compact.size(); i++) {
          error_s s = storage->Compact(t.compact[i]);
          if (!s.is_ok()) {
            ASSERT_FALSE(t.wallow);
          } else {
            ASSERT_EQ(log.allEntries().size(), t.wleft[i]);
          }
        }
      } catch (RaftError &e) {
        ASSERT_FALSE(t.wallow);
      }
    }
  }

  void TestTerm() {
    uint64_t offset = 100;
    uint64_t num = 100;

    auto storage = new MemoryStorage();
    storage->ApplySnapshot(idl::Snapshot().metadata_index(offset).metadata_term(1));
    for (int i = 1; i < num; i++) {
      idl::EntryVec v{newEntry(offset + i, i)};
      storage->Append(v);
    }

    struct TestData {
      uint64_t index;
      uint64_t wterm;
    } tests[] = {
        {offset - 1, 0},
        {offset, 1},
        {offset + num / 2, num / 2},
        {offset + num - 1, num - 1},
        {offset + num, 0},
    };

    RaftLog log(storage);

    for (auto t : tests) {
      ASSERT_EQ(mustTerm(log, t.index), t.wterm) << t.index << " " << t.wterm;
    }
  }

  void TestTermWithUnstableSnapshot() {
    uint64_t storagesnapi = 100;
    uint64_t unstablesnapi = storagesnapi + 5;

    auto storage = new MemoryStorage;
    storage->ApplySnapshot(idl::Snapshot().metadata_index(storagesnapi).metadata_term(1));

    RaftLog log(storage);
    log.restore(idl::Snapshot().metadata_index(unstablesnapi).metadata_term(1));

    struct TestData {
      uint64_t index;
      uint64_t w;
    } tests[] = {
        // cannot get term from storage
        {storagesnapi, 0},
        // cannot get term from the gap between storage ents and unstable snapshot
        {storagesnapi + 1, 0},
        {unstablesnapi - 1, 0},
        // get term from unstable snapshot index
        {unstablesnapi, 1},
    };

    for (auto t : tests) {
      ASSERT_EQ(mustTerm(log, t.index), t.w);
    }
  }

  void TestLogRestore() {
    uint64_t index = 1000;
    uint64_t term = 1000;
    auto snap = idl::Snapshot().metadata_index(index).metadata_term(term);
    auto storage = new MemoryStorage;
    storage->ApplySnapshot(snap);
    RaftLog log(storage);

    ASSERT_TRUE(log.allEntries().empty());
    ASSERT_EQ(log.firstIndex(), index + 1);
    ASSERT_EQ(log.committed_, index);
    ASSERT_EQ(log.unstable_.offset, index + 1);
    ASSERT_EQ(mustTerm(log, index), term);
  }

  void TestIsOutOfBounds() {
    uint64_t offset = 100;
    uint64_t num = 100;
    auto storage = new MemoryStorage();
    storage->ApplySnapshot(idl::Snapshot().metadata_index(offset));
    RaftLog l(storage);
    for (uint64_t i = 1; i <= num; i++) {
      idl::EntryVec v{newEntry(i + offset, 1)};
      l.append(v);
    }

    uint64_t first = offset + 1;
    struct TestData {
      uint64_t lo, hi;
      bool wpanic;
      bool wErrCompacted;
    } tests[] = {
        {
            first - 2,
            first + 1,
            false,
            true,
        },
        {
            first - 1,
            first + 1,
            false,
            true,
        },
        {
            first,
            first,
            false,
            false,
        },
        {
            first + num / 2,
            first + num / 2,
            false,
            false,
        },
        {
            first + num - 1,
            first + num - 1,
            false,
            false,
        },
        {
            first + num,
            first + num,
            false,
            false,
        },
        {
            first + num,
            first + num + 1,
            true,
            false,
        },
        {
            first + num + 1,
            first + num + 1,
            true,
            false,
        },
    };

    for (auto tt : tests) {
      try {
        error_s err = l.mustCheckOutOfBounds(tt.lo, tt.hi);
        ASSERT_FALSE(tt.wpanic);
        if (tt.wErrCompacted) {
          ASSERT_EQ(err, ErrCompacted);
        } else {
          ASSERT_TRUE(err.is_ok());
        }
      } catch (RaftError &e) {
        ASSERT_TRUE(tt.wpanic);
      }
    }
  }
};  // namespace yaraft

TEST_F(RaftLogTest, FindConflict) {
  TestFindConflict();
}

TEST_F(RaftLogTest, IsUpToDate) {
  TestIsUpToDate();
}

TEST_F(RaftLogTest, Append) {
  TestAppend();
}

TEST_F(RaftLogTest, MaybeAppend) {
  TestLogMaybeAppend();
}

TEST_F(RaftLogTest, CompactionSideEffects) {
  TestCompactionSideEffects();
}

TEST_F(RaftLogTest, HasNextEnts) {
  TestHasNextEnts();
}

TEST_F(RaftLogTest, NextEnts) {
  TestNextEnts();
}

TEST_F(RaftLogTest, UnstableEnts) {
  TestUnstableEnts();
}

TEST_F(RaftLogTest, CommitTo) {
  TestCommitTo();
}

TEST_F(RaftLogTest, StableTo) {
  TestStableTo();
}

TEST_F(RaftLogTest, StableToWithSnap) {
  TestStableToWithSnap();
}

TEST_F(RaftLogTest, Compaction) {
  TestCompaction();
}

TEST_F(RaftLogTest, Restore) {
  TestLogRestore();
}

TEST_F(RaftLogTest, MustCheckOutOfBound) {
  TestIsOutOfBounds();
}

TEST_F(RaftLogTest, Term) {
  TestTerm();
}

TEST_F(RaftLogTest, TermWithUnstableSnapshot) {
  TestTermWithUnstableSnapshot();
}

TEST_F(RaftLogTest, Slice) {
  TestSlice();
}

}  // namespace yaraft
