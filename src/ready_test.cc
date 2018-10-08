// Copyright 2017 Wu Tao
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

#include <yaraft/ready.h>

#include "test_utils.h"

namespace yaraft {

class ReadyTest : public BaseTest {
 public:
  void TestReadyContainUpdates() {
    {
      Ready rd;
      ASSERT_FALSE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.softState = make_unique<SoftState>();
      rd.softState->lead = 1;
      ASSERT_TRUE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.hardState.vote(1);
      ASSERT_TRUE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.entries = idl::EntryVec::make(1);
      ASSERT_TRUE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.committedEntries = idl::EntryVec::make(1);
      ASSERT_TRUE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.messages.resize(1);
      ASSERT_TRUE(rd.containsUpdates());
    }

    {
      Ready rd;
      rd.snapshot.metadata_index(1);
      ASSERT_TRUE(rd.containsUpdates());
    }
  }
};

TEST_F(ReadyTest, ContainUpdates) {
  TestReadyContainUpdates();
}

}  // namespace yaraft
