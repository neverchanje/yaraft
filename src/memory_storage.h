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

#include <mutex>
#include <vector>

#include "raftpb.pb.h"
#include "status.h"
#include "storage.h"

#include <glog/logging.h>
#include <silly/disallow_copying.h>

namespace yaraft {

// MemoryStorage implements the Storage interface backed by an
// in-memory array.
//
// Thread-safe.
class MemoryStorage : public Storage {
  __DISALLOW_COPYING__(MemoryStorage);

 public:
  // NOTE: Except for MemoryStorage::Term, all operations on MemoryStorage is not allowed to place
  // an index beyond LastIndex(), it'll panic when this happens. But index is allowed to be pointed
  // before FirstIndex(), it'll return error status though, but it's deterministic.
  // The point is: we can request to read stale data, but we cannot read data that does not exist.
  virtual StatusWith<uint64_t> Term(uint64_t i) const override {
    std::lock_guard<std::mutex> guard(mu_);

    auto beginIndex = entries_.begin()->index();

    if (i <= beginIndex) {
      return Status::Make(Error::LogCompacted);
    }

    if (i > entries_.rbegin()->index()) {
      return Status::Make(Error::Overflow);
    }

    return StatusWith<uint64_t>(entries_[i - beginIndex].term());
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
                                                     uint64_t maxSize) override {
    using StatusWithEntryVec = StatusWith<std::vector<pb::Entry>>;

    DLOG_ASSERT(lo <= hi);

    std::lock_guard<std::mutex> guard(mu_);
    if (lo <= entries_.begin()->index()) {
      return Status::Make(Error::LogCompacted);
    }

    LOG_ASSERT(hi - 1 <= entries_.rbegin()->index());

    if (entries_.size() == 1) {
      // contains only a dummy entry
      return StatusWithEntryVec(Error::Overflow);
    }

    std::vector<pb::Entry> ret;
    ret.reserve(hi - lo);
    uint64_t loOffset = lo - entries_.begin()->index();

    ret.push_back(entries_[loOffset]);
    uint64_t size = entries_[loOffset].ByteSize();

    for (int i = 1; i < hi - lo; i++) {
      size += entries_[i + loOffset].ByteSize();
      if (size > maxSize)
        break;
      ret.push_back(entries_[i + loOffset]);
    }

    return StatusWithEntryVec(ret);
  }

 public:
  MemoryStorage() {
    // When starting from scratch populate the list with a dummy entry at term zero.
    entries_.push_back(pb::Entry());
  }

  // Compact discards all log entries prior to compactIndex.
  // It is the application's responsibility to not attempt to compact an index
  // greater than raftLog.applied.
  Status Compact(uint64_t compactIndex) {
    uint64_t beginIndex = entries_.begin()->index();
    if (compactIndex <= beginIndex) {
      return Status::Make(Error::LogCompacted);
    }
    LOG_ASSERT(compactIndex <= entries_.rbegin()->index());

    size_t compactOffset = compactIndex - beginIndex;

    pb::Entry tmp;
    tmp.set_term(entries_[compactOffset].term());
    tmp.set_index(entries_[compactOffset].index());
    entries_[0].Swap(&tmp);

    size_t l = 1;
    for (size_t i = compactOffset + 1; i < entries_.size(); i++) {
      entries_[l++].Swap(&entries_[i]);
    }

    entries_.resize(l);
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
      return StatusWith<pb::Snapshot *>(Error::SnapshotOutOfDate);
    }
    if (cs) {
    }
    LOG_ASSERT(i <= entries_.rbegin()->index());
  }

 public:
  /// The following functions are for test only.

  std::vector<pb::Entry> &TEST_Entries() {
    return entries_;
  }

  static MemoryStorage *TEST_Empty() {
    auto tmp = new MemoryStorage();
    tmp->entries_.clear();
    return tmp;
  }

 private:
  pb::HardState hard_state_;
  pb::Snapshot snapshot_;

  // Operations like Storage::Term, Storage::Entries require random access of the
  // underlying data structure. In terms of performance, we choose
  // std::vector to store entries.
  std::vector<pb::Entry> entries_;

  mutable std::mutex mu_;
};

}  // namespace yaraft
