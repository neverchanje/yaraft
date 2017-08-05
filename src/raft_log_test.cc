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
#include "raft_log.h"
#include "test_utils.h"

#include <gtest/gtest.h>

using namespace yaraft;

uint64_t mustTerm(const RaftLog& log, uint64_t index) {
  auto s = log.Term(index);
  if (!s.IsOK()) {
    return 0;
  }
  return s.GetValue();
}

TEST(RaftLog, IsUpToDate) {}

TEST(RaftLog, Term) {
  uint64_t offset = 100;
  uint64_t num = 100;

  struct TestData {
    uint64_t index;
    uint64_t wterm;
  } tests[] = {
      {offset - 1, 0},   {offset, 1}, {offset + num / 2, num / 2}, {offset + num - 1, num - 1},
      {offset + num, 0},
  };

  auto storage = new MemoryStorage();
  storage->ApplySnapshot(PBSnapshot().MetaIndex(offset).MetaTerm(1).v);
  for (int i = 1; i < num; i++)
    storage->Append(pbEntry(uint64_t(offset + i), uint64_t(i)));

  RaftLog log(storage);

  for (auto t : tests) {
    ASSERT_EQ(mustTerm(log, t.index), t.wterm) << t.index << " " << t.wterm;
  }
}

TEST(RaftLog, TermWithUnstableSnapshot) {
  uint64_t storageSnapshotIndex = 100;
  uint64_t unstableSnapshotIndex = storageSnapshotIndex + 5;

  auto snap = PBSnapshot().MetaIndex(storageSnapshotIndex).MetaTerm(1).v;
  auto storage = new MemoryStorage;
  storage->ApplySnapshot(snap);

  RaftLog log(storage);
  snap = PBSnapshot().MetaIndex(unstableSnapshotIndex).MetaTerm(1).v;
  log.Restore(snap);

  struct TestData {
    uint64_t index;
    uint64_t wterm;
  } tests[] = {
      // cannot get term from storage
      {storageSnapshotIndex, 0},

      // cannot get term from the gap between storage ents and unstable snapshot
      {storageSnapshotIndex + 1, 0},
      {unstableSnapshotIndex - 1, 0},

      // get term from unstable snapshot index
      {unstableSnapshotIndex, 1},
  };

  for (auto t : tests) {
    ASSERT_EQ(mustTerm(log, t.index), t.wterm);
  }
}

TEST(RaftLog, Append) {
  struct TestData {
    EntryVec ents;

    uint64_t windex;
    EntryVec wents;
    uint64_t wunstable;
  } tests[] = {
      {{}, 2, pbEntry(1, 1) + pbEntry(2, 2)},
      {{pbEntry(3, 2)}, 3, pbEntry(1, 1) + pbEntry(2, 2) + pbEntry(3, 2)},

      // conflicts with index 1
      {{pbEntry(1, 2)}, 1, {pbEntry(1, 2)}},

      // conflicts with index 2
      {pbEntry(2, 3) + pbEntry(3, 3), 3, pbEntry(1, 1) + pbEntry(2, 3) + pbEntry(3, 3)},
  };

  for (auto t : tests) {
    auto storage = new MemoryStorage();
    storage->Append(pbEntry(1, 1) + pbEntry(2, 2));

    RaftLog log(storage);

    log.Append(t.ents);
    uint64_t index = log.LastIndex();
    ASSERT_EQ(index, t.windex);

    auto s = log.Entries(1, log.LastIndex() + 1, noLimit);
    ASSERT_TRUE(s.IsOK());
    EntryVec_ASSERT_EQ(s.GetValue(), t.wents);
  }
}

TEST(RaftLog, Entries) {
  uint64_t offset = 100;
  uint64_t num = 100;
  uint64_t last = offset + num;
  uint64_t half = offset + num / 2;
  auto halfe = pbEntry(half, half);

  auto storage = new MemoryStorage();
  storage->ApplySnapshot(PBSnapshot().MetaIndex(offset).v);
  for (uint64_t i = 1; i < num / 2; i++) {
    storage->Append(pbEntry(offset + i, offset + i));
  }

  RaftLog log(storage);
  for (uint64_t i = num / 2; i < num; i++) {
    log.Append(pbEntry(offset + i, offset + i));
  }

  struct TestData {
    uint64_t from, to, limit;

    EntryVec w;
  } tests[] = {
      // test no limit
      {offset - 1, offset + 1, noLimit, {}},
      {offset, offset + 1, noLimit, {}},
      {half - 1, half + 1, noLimit, {pbEntry(half - 1, half - 1), pbEntry(half, half)}},
      {half, half + 1, noLimit, {pbEntry(half, half)}},
      {last - 1, last, noLimit, {pbEntry(last - 1, last - 1)}},

      // TODO test limit
  };

  for (auto t : tests) {
    auto s = log.Entries(t.from, t.to, t.limit);
    if (t.from <= offset) {
      ASSERT_EQ(s.GetStatus().Code(), Error::LogCompacted);
      continue;
    }
    EntryVec_ASSERT_EQ(s.GetValue(), t.w);
  }
}

// RaftLog.MaybeAppend ensures:
// If the given (index, term) matches with the existing log:
// 	1. If an existing entry conflicts with a new one (same index
// 	but different terms), delete the existing entry and all that
// 	follow it
// 	2. Append any new entries not already in the log
// If the given (index, term) does not match with the existing log:
// 	return false
TEST(RaftLog, MaybeAppend) {
  uint64_t prevIndex = 3;
  uint64_t prevTerm = 3;

  struct TestData {
    uint64_t logTerm;
    uint64_t index;
    EntryVec ents;

    uint64_t wlasti;
    bool wappend;
    bool wpanic;
  } tests[] = {
      // not match: term is different
      {prevTerm - 1, prevIndex, {pbEntry(prevIndex + 1, prevTerm + 1)}, 0, false, false},

      // not match: index out of bound
      {prevTerm, prevIndex + 1, {pbEntry(prevIndex + 2, prevTerm + 1)}, 0, false, false},

      // match with the last existing entry
      {prevTerm, prevIndex, {}, prevIndex, true, false},
      {prevTerm, prevIndex, {pbEntry(prevIndex + 1, prevTerm + 1)}, prevIndex + 1, true, false},
      {prevTerm - 1, prevIndex - 1, {pbEntry(prevIndex, prevTerm)}, prevIndex, true, false},
      {prevTerm - 2, prevIndex - 2, {pbEntry(prevIndex - 1, prevTerm)}, prevIndex - 1, true, false},
      {
          prevTerm - 3, prevIndex - 3, {pbEntry(prevIndex - 2, prevTerm)}, 0, false, true,
      },  // conflict with existing committed entry
      {
          prevTerm - 2,
          prevIndex - 2,
          {pbEntry(prevIndex - 1, prevTerm), pbEntry(prevIndex, prevTerm)},
          prevIndex,
          true,
          false,
      },
  };

  for (auto t : tests) {
    RaftLog log(new MemoryStorage());
    log.Append(pbEntry(1, 1) + pbEntry(2, 2) + pbEntry(3, 3));
    log.CommitTo(1);

    bool panic = false;
    try {
      uint64_t newLastIndex = 0;
      auto msg = PBMessage().Index(t.index).LogTerm(t.logTerm).Entries(t.ents).v;
      bool append = log.MaybeAppend(msg, &newLastIndex);
      ASSERT_EQ(t.wlasti, newLastIndex);
      ASSERT_EQ(t.wappend, append);
      if (append && !t.ents.empty()) {
        auto s = log.Entries(log.LastIndex() - t.ents.size() + 1, log.LastIndex() + 1, noLimit);
        ASSERT_TRUE(s.IsOK());
        EntryVec_ASSERT_EQ(s.GetValue(), t.ents);
      }
    } catch (RaftError& e) {
      panic = true;
    }
    ASSERT_EQ(panic, t.wpanic);
  }
}

TEST(RaftLog, Restore) {
  uint64_t index = 1000;
  uint64_t term = 1000;

  auto snap = PBSnapshot().MetaIndex(index).MetaTerm(term).v;

  auto storage = new MemoryStorage;
  storage->ApplySnapshot(snap);

  RaftLog log(storage);
  ASSERT_EQ(log.CommitIndex(), index);
  ASSERT_EQ(log.GetUnstable().offset, index + 1);
  ASSERT_EQ(log.Term(index).GetValue(), term);
}

TEST(RaftLog, Compaction) {
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
      storage->Append(PBEntry().Index(i).Term(1).v);
    }

    RaftLog log(storage);
    if (log.ZeroTermOnErrCompacted(t.lastIndex) == 1) {
      log.CommitTo(t.lastIndex);
    }
    log.ApplyTo(log.CommitIndex());

    try {
      for (int i = 0; i < t.compact.size(); i++) {
        Status s = storage->Compact(t.compact[i]);
        if (!s.IsOK()) {
          ASSERT_FALSE(t.wallow);
        } else {
          ASSERT_EQ(log.AllEntries().size(), t.wleft[i]);
        }
      }
    } catch (std::exception& e) {
      LOG(ERROR) << e.what();
      ASSERT_FALSE(t.wallow);
    }
  }
}