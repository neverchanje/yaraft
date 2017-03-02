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

#include <gtest/gtest.h>

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
