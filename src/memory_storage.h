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

#include "pb_helper.h"
#include "raftpb.pb.h"
#include "status.h"
#include "storage.h"

#include <glog/logging.h>
#include <silly/disallow_copying.h>

namespace yaraft {

// MemoryStorage implements the Storage interface backed by an
// in-memory array.
// NOTE: MemoryStorage will only be used in tests.
//
// Thread-safe.
class MemoryStorage : public Storage {
  __DISALLOW_COPYING__(MemoryStorage);

 public:
  virtual StatusWith<uint64_t> Term(uint64_t i) const override {
    std::lock_guard<std::mutex> guard(mu_);

    auto beginIndex = entries_.begin()->index();

    if (i < beginIndex) {
      return Status::Make(Error::LogCompacted);
    }

    if (i > entries_.rbegin()->index()) {
      return Status::Make(Error::OutOfBound);
    }

    return StatusWith<uint64_t>(entries_[i - beginIndex].term());
  }

  virtual StatusWith<uint64_t> FirstIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return firstIndex();
  }

  virtual StatusWith<uint64_t> LastIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return lastIndex();
  }

  virtual StatusWith<EntryVec> Entries(uint64_t lo, uint64_t hi, uint64_t maxSize) override {
    DLOG_ASSERT(lo <= hi);

    std::lock_guard<std::mutex> guard(mu_);
    if (lo <= entries_.begin()->index()) {
      return Status::Make(Error::LogCompacted);
    }

    LOG_ASSERT(hi - 1 <= entries_.rbegin()->index());

    if (entries_.size() == 1) {
      // contains only a dummy entry
      return Status::Make(Error::OutOfBound);
    }

    uint64_t loOffset = lo - entries_.begin()->index();
    int size = entries_[loOffset].ByteSize();

    std::vector<pb::Entry> ret;
    ret.push_back(entries_[loOffset]);

    for (int i = 1; i < hi - lo; i++) {
      size += entries_[i + loOffset].ByteSize();
      if (size > maxSize)
        break;
      ret.push_back(entries_[i + loOffset]);
    }

    return ret;
  }

  virtual StatusWith<pb::Snapshot> Snapshot() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return snapshot_;
  }

  virtual Status InitialState(pb::HardState *hardState, pb::ConfState *confState) override {
    *hardState = hardState_;
    return Status::OK();
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
    hardState_.Swap(&st);
  }

  // Append the new entries to storage.
  void Append(pb::Entry entry) {
    std::lock_guard<std::mutex> guard(mu_);
    unsafeAppend(entry);
  }

  void Append(EntryVec entries) {
    std::lock_guard<std::mutex> guard(mu_);
    if (entries.empty())
      return;

    for (auto &e : entries) {
      unsafeAppend(e);
    }

    // corner case
    uint64_t end = entries.rbegin()->index();
    if (end < lastIndex() && end >= firstIndex()) {
      // truncate the existing entries
      entries_.resize(end - entries_.begin()->index() + 1);
    }
  }

  // ApplySnapshot overwrites the contents of this Storage object with
  // those of the given snapshot.
  void ApplySnapshot(pb::Snapshot &snap) {
    std::lock_guard<std::mutex> guard(mu_);
    snapshot_.Swap(&snap);
    entries_.clear();
    entries_.push_back(
        PBEntry().Term(snapshot_.metadata().term()).Index(snapshot_.metadata().index()).v);
  }

 private:
  uint64_t firstIndex() const {
    return entries_.begin()->index() + 1;
  }

  uint64_t lastIndex() const {
    return entries_.rbegin()->index();
  }

  void unsafeAppend(pb::Entry &entry) {
    auto first = firstIndex();
    auto last = lastIndex();
    auto index = entry.index();
    if (index < first)
      return;
    if (index > last) {
      // ensures the entries are continuous.
      DLOG_ASSERT(index - last == 1);
      entries_.push_back(std::move(entry));
      return;
    }

    // replace the old record if overlapped.
    auto offset = entry.index() - entries_.begin()->index();
    entries_[offset] = std::move(entry);
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
  pb::HardState hardState_;
  pb::Snapshot snapshot_;

  // Operations like Storage::Term, Storage::Entries require random access of the
  // underlying data structure. In terms of performance, we choose
  // std::vector to store entries.
  std::vector<pb::Entry> entries_;

  mutable std::mutex mu_;
};

using MemStoreUptr = std::unique_ptr<MemoryStorage>;

}  // namespace yaraft
