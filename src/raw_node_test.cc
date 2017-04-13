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

#include "raw_node.h"
#include "test_utils.h"

using namespace yaraft;

// This test ensures that RawNode.Step ignore local message.
TEST(RawNode, Step) {
  for (int i = pb::MessageType_MIN; i <= pb::MessageType_MAX; i++) {
    RawNode rn(newTestConfig(1, {1}, 10, 1, new MemoryStorage()));
    auto s = rn.Step(PBMessage().From(1).To(1).Type(static_cast<pb::MessageType>(i)).v);
    ASSERT_EQ(s.Code(), Error::StepLocalMsg);
  }
}