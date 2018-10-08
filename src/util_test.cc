// Copyright 2015 The etcd Authors
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

#include "util.h"
#include "test_utils.h"

namespace yaraft {

class UtilTest : public BaseTest {
 public:
  void TestLimitSize() {
    auto ents = idl::EntryVec{newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)};

    struct TestData {
      uint64_t maxsize;
      idl::EntryVec wentries;
    } tests[] = {
        {std::numeric_limits<uint64_t>::max(), {newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)}},
        // even if maxsize is zero, the first entry should be returned
        {0, {newEntry(4, 4)}},
        // limit to 2
        {ents[0].ByteSize() + ents[1].ByteSize(), {newEntry(4, 4), newEntry(5, 5)}},
        // limit to 2
        {ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize() / 2, {newEntry(4, 4), newEntry(5, 5)}},
        {ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize() - 1, {newEntry(4, 4), newEntry(5, 5)}},
        // all
        {ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize(), {newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)}},
    };

    for (auto tt : tests) {
      auto result = limitSize(tt.maxsize, ents);
      EntryVec_ASSERT_EQ(result, tt.wentries);
    }
  }
};

TEST_F(UtilTest, LimitSize) {
  TestLimitSize();
}

}  // namespace yaraft
