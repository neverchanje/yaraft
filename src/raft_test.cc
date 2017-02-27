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

#include "raft.h"
#include "memory_storage.h"
#include "test_utils.h"

#include <gtest/gtest.h>

using namespace yaraft;

// Ensure that the Step function ignores the message from old term and does not pass it to the
// actual stepX function.
TEST(Raft, StepIgnoreOldTermMsg) {
  std::unique_ptr<Raft> raft(newTestRaft(1, {1}, 10, 1, new MemoryStorage()));

  bool called = false;
  raft->step_ = [&](const pb::Message& m) { called = true; };

  raft->TEST_SetTerm(2);

  pb::Message m;
  m.set_term(raft->Term() - 1);
  m.set_type(pb::MsgApp);
  raft->Step(m);
  ASSERT_FALSE(called);
}