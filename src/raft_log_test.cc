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

#include "raft_log.h"
#include "memory_storage.h"
#include "test_utils.h"

using namespace yaraft;

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

    uint64_t index = log.Append(t.ents);
    ASSERT_EQ(index, t.windex);

    EntryVec ents;
    auto s = log.Entries(1, log.LastIndex() + 1, noLimit);
    ASSERT_TRUE(s.OK());
    ents = s.GetValue();
    ASSERT_TRUE(ents == t.wents);
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
    ASSERT_TRUE(s.GetValue() == t.w);
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
        ASSERT_TRUE(s.OK());
        ASSERT_TRUE(s.GetValue() == t.ents);
      }
    } catch (RaftError &e) {
      panic = true;
    }
    ASSERT_EQ(panic, t.wpanic);
  }
}