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

#include <gtest/gtest.h>

#include "memory_storage.h"

using namespace yaraft;

pb::Entry pbEntry(uint64_t index, uint64_t term) {
  pb::Entry tmp;
  tmp.set_term(term);
  tmp.set_index(index);
  return tmp;
}

std::vector<pb::Entry> &operator<<(std::vector<pb::Entry> &v, pb::Entry e) {
  v.push_back(e);
  return v;
}

TEST(MemoryStorage, Term) {
  MemoryStorage storage;
}

TEST(MemoryStorage, Compact) {
  struct TestData {
    int i;

    Error::ErrorCodes werr;
    uint64_t windex;
    uint64_t wterm;
    int wlen;
  } tests[] = {TestData{2, Error::LogCompacted, 3, 3, 3}, TestData{3, Error::LogCompacted, 3, 3, 3},
               TestData{4, Error::OK, 4, 4, 2}, TestData{5, Error::OK, 5, 5, 1}};

  for (auto t : tests) {
    MemoryStorage storage;
    storage.TEST_Entries() << pbEntry(3, 3) << pbEntry(4, 4) << pbEntry(5, 5);
    auto status = storage.Compact(t.i);
    ASSERT_EQ(status.Code(), t.werr);
    ASSERT_EQ(storage.TEST_Entries()[0].index(), t.windex);
    ASSERT_EQ(storage.TEST_Entries()[0].term(), t.wterm);
  }
}
