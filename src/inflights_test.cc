
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

#include "inflights.h"
#include "test_utils.h"

namespace yaraft {

#define ASSERT_Inflight_EQ(lhs, rhs)       \
  do {                                     \
    const auto& _lhs = (lhs);              \
    const auto& _rhs = (rhs);              \
    ASSERT_EQ(_lhs.start_, _rhs.start_);   \
    ASSERT_EQ(_lhs.count_, _rhs.count_);   \
    ASSERT_EQ(_lhs.buffer_, _rhs.buffer_); \
  } while (0)

class InflightsTest : public BaseTest {
 public:
  void TestInflightsAdd() {
    // no rotating case
    Inflights in(10);
    for (uint64_t i = 0; i < 5; i++) {
      in.add(i);
    }

    Inflights wantIn;
    wantIn.start_ = 0;
    wantIn.count_ = 5;
    //                ↓------------
    wantIn.buffer_ = {0, 1, 2, 3, 4};
    ASSERT_Inflight_EQ(in, wantIn);

    for (uint64_t i = 5; i < 10; i++) {
      in.add(i);
    }
    Inflights wantIn2;
    wantIn2.start_ = 0;
    wantIn2.count_ = 10;
    //                 ↓---------------------------
    wantIn2.buffer_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ASSERT_Inflight_EQ(in, wantIn2);

    // rotating case
    Inflights in2;
    in2.start_ = 5;
    in2.buffer_ = {0, 0, 0, 0, 0};
    in2.buffer_.reserve(10);
    for (uint64_t i = 0; i < 5; i++) {
      in2.add(i);
    }
    Inflights wantIn21;
    wantIn21.start_ = 5;
    wantIn21.count_ = 5;
    //                                 ↓------------
    wantIn21.buffer_ = {0, 0, 0, 0, 0, 0, 1, 2, 3, 4};
    ASSERT_Inflight_EQ(in2, wantIn21);

    for (uint64_t i = 5; i < 10; i++) {
      in2.add(i);
    }
    Inflights wantIn22;
    wantIn22.start_ = 5;
    wantIn22.count_ = 10;
    //                  -------------- ↓------------
    wantIn22.buffer_ = {5, 6, 7, 8, 9, 0, 1, 2, 3, 4};
    ASSERT_Inflight_EQ(in2, wantIn22);
  }

  void TestInflightFreeTo() {
    // no rotating case
    Inflights in(10);
    for (uint64_t i = 0; i < 10; i++) {
      in.add(i);
    }

    in.freeTo(4);

    Inflights wantIn(10);
    wantIn.start_ = 5;
    wantIn.count_ = 5;
    //                               ↓------------
    wantIn.buffer_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ASSERT_Inflight_EQ(in, wantIn);

    in.freeTo(8);

    Inflights wantIn2(10);
    wantIn2.start_ = 9;
    wantIn2.count_ = 1;
    //                                            ↓
    wantIn2.buffer_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ASSERT_Inflight_EQ(in, wantIn2);

    // rotating case
    for (uint64_t i = 10; i < 15; i++) {
      in.add(i);
    }
    in.freeTo(12);

    Inflights wantIn3(10);
    wantIn3.start_ = 3;
    wantIn3.count_ = 2;
    //                            ↓-----
    wantIn3.buffer_ = {10, 11, 12, 13, 14, 5, 6, 7, 8, 9};
    ASSERT_Inflight_EQ(in, wantIn3);

    in.freeTo(14);

    Inflights wantIn4(10);
    wantIn4.start_ = 0;
    wantIn4.count_ = 0;
    //                  ↓
    wantIn4.buffer_ = {10, 11, 12, 13, 14, 5, 6, 7, 8, 9};
    ASSERT_Inflight_EQ(in, wantIn4);
  }

  void TestInflightFreeFirstOne() {
    Inflights in(10);
    for (uint64_t i = 0; i < 10; i++) {
      in.add(i);
    }

    in.freeFirstOne();

    Inflights wantIn;
    wantIn.start_ = 1;
    wantIn.count_ = 9;
    //                   ↓------------------------
    wantIn.buffer_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    ASSERT_Inflight_EQ(in, wantIn);
  }
};

TEST_F(InflightsTest, Add) {
  TestInflightsAdd();
}

TEST_F(InflightsTest, FreeTo) {
  TestInflightFreeTo();
}

TEST_F(InflightsTest, FreeFirstOne) {
  TestInflightFreeFirstOne();
}

}  // namespace yaraft
