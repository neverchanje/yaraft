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

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yaraft/logger.h>

namespace yaraft {

class Inflights {
 public:
  explicit Inflights(size_t c) {
    buffer_.reserve(c);
  }

  // add adds an inflight into inflights
  void add(uint64_t inflight) {
    if (full()) {
      RAFT_PANIC("cannot add into a full inflights");
    }
    size_t next = start_ + count_;
    if (next >= cap()) {
      next -= cap();
    }
    RAFT_ASSERT(next <= buffer_.size());
    if (next == buffer_.size()) {
      buffer_.push_back(inflight);
    } else {
      buffer_[next] = inflight;
    }
    count_++;
  }

  // freeTo frees the inflights smaller or equal to the given `to` flight.
  void freeTo(uint64_t to) {
    if (count_ == 0 || to < buffer_[start_]) {
      // out of the left side of the window
      return;
    }

    size_t idx = start_;
    int i;
    for (i = 0; i < count_; i++) {
      if (to < buffer_[idx]) {  // found the first large inflight
        break;
      }

      // increase index and maybe rotate
      idx++;
      if (idx >= cap()) {
        idx -= cap();
      }
    }

    // free i inflights and set new start index
    count_ -= i;
    start_ = idx;
    if (count_ == 0) {
      // inflights is empty, reset the start index so that we don't grow the
      // buffer unnecessarily.
      start_ = 0;
    }
  }

  void freeFirstOne() {
    freeTo(buffer_[start_]);
  }

  bool full() const {
    return count_ == cap();
  }

  // reset frees all inflights.
  void reset() {
    count_ = 0;
    start_ = 0;
  }

 private:
  friend class InflightsTest;

  Inflights() = default;

  // the starting index in the buffer
  size_t start_{0};

  // number of inflights in the buffer
  size_t count_{0};

  // the size of the buffer
  size_t cap() const { return buffer_.capacity(); }

  // buffer contains the index of the last entry
  // inside one message
  std::vector<uint64_t> buffer_;
};

}  // namespace yaraft
