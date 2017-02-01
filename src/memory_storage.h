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

#include <deque>
#include <glog/logging.h>
#include <mutex>

#include "raftpb.pb.h"
#include "status.h"
#include "storage.h"

namespace yaraft {

// MemoryStorage implements the Storage interface backed by an
// in-memory array.
//
// Thread-safe.
class MemoryStorage : public Storage {
 public:
  virtual StatusWith<uint64_t> Term(uint64_t i) const override {
    std::lock_guard<std::mutex> guard(mu_);

    if (i < entries_.begin()->index()) {
      return Status::Make(Error::LogCompacted);
    }

    if (i > entries_.size()) {
    }
  }

  virtual StatusWith<uint64_t> FirstIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return StatusWith<uint64_t>(entries_.begin()->index() + 1);
  }

  virtual StatusWith<uint64_t> LastIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return StatusWith<uint64_t>(entries_.rbegin()->index());
  }

  virtual StatusWith<pb::Snapshot> Snapshot() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return snapshot_;
  }

  virtual StatusWith<std::vector<pb::Entry>> Entries(uint64_t lo, uint64_t hi,
                                                     uint64_t maxSize) override {}

 public:
  MemoryStorage() {
    // When starting from scratch populate the list with a dummy entry at term zero.
    // TODO: I've no idea what this dummy entry is used for.
    entries_.push_back(pb::Entry());
  }

  // Compact discards all log entries prior to compactIndex.
  // It is the application's responsibility to not attempt to compact an index
  // greater than raftLog.applied.
  Status Compact(uint64_t compactIndex) {
    auto offset = entries_.begin()->index();
    if (compactIndex <= offset) {
      return Status::Make(Error::LogCompacted);
    }
    LOG_ASSERT(compactIndex <= entries_.rbegin()->index());

    for (int i = 0; i < compactIndex - offset - 1; i++) {
      entries_.pop_front();
    }
    return Status::OK();
  }

  // SetHardState saves the current HardState.
  void SetHardState(pb::HardState st) {
    std::lock_guard<std::mutex> guard(mu_);
    hard_state_.Swap(&st);
  }

  // CreateSnapshot makes a snapshot which can be retrieved with Snapshot() and
  // can be used to reconstruct the state at that point.
  // If any configuration changes have been made since the last compaction,
  // the result of the last ApplyConfChange must be passed in.
  StatusWith<pb::Snapshot *> CreateSnapshot(uint64_t i, pb::ConfState *cs, char data[]) const {
    std::lock_guard<std::mutex> guard(mu_);
    if (i <= snapshot_.metadata().index()) {
      return StatusWith<pb::Snapshot *>(Status::Make(Error::SnapshotOutOfDate));
    }
    if (cs) {
    }
    LOG_ASSERT(i <= entries_.rbegin()->index());
  }

 private:
  pb::HardState hard_state_;
  pb::Snapshot snapshot_;
  std::deque<pb::Entry> entries_;

  mutable std::mutex mu_;
};

}  // namespace yaraft
