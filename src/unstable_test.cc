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

using namespace yaraft;

TEST(Unstable, TruncateAndAppend) {
  struct TestData {
    std::vector<pb::Entry> entries;
    std::vector<pb::Entry> toAppend;

    std::vector<pb::Entry> wentries;
  } tests[] = {
      {
          // append to the end
          {pbEntry(5, 1)},
          {pbEntry(6, 1), pbEntry(7, 1)},
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
      },
      // replace the unstable entries
      {
          {pbEntry(5, 1)}, {pbEntry(5, 2), pbEntry(6, 2)}, {pbEntry(5, 2), pbEntry(6, 2)},
      },
      {
          {pbEntry(5, 1)},
          {pbEntry(4, 2), pbEntry(5, 2), pbEntry(6, 2)},
          {pbEntry(4, 2), pbEntry(5, 2), pbEntry(6, 2)},
      },
      // truncate the existing entries and append
      {
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
          {pbEntry(6, 2)},
          {pbEntry(5, 1), pbEntry(6, 2)},
      },
      {
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 1)},
          {pbEntry(7, 2), pbEntry(8, 2)},
          {pbEntry(5, 1), pbEntry(6, 1), pbEntry(7, 2), pbEntry(8, 2)},
      },
  };

  for (auto t : tests) {
    Unstable u;
    u.entries = t.entries;

    auto msg = PBMessage().Entries(t.toAppend).v;
    u.TruncateAndAppend(msg.mutable_entries()->begin(), msg.mutable_entries()->end());
    ASSERT_TRUE(u.entries == t.wentries);
  }
}