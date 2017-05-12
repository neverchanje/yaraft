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

#include "fluent_pb.h"
#include "raftpb.pb.h"
#include "status.h"
#include "storage.h"

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
  virtual StatusWith<uint64_t> Term(uint64_t i) const override;

  virtual StatusWith<uint64_t> FirstIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return firstIndex();
  }

  virtual StatusWith<uint64_t> LastIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return lastIndex();
  }

  virtual StatusWith<EntryVec> Entries(uint64_t lo, uint64_t hi, uint64_t *maxSize) override;

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
    // When starting from scratch populate the list with a dummy entry at term zero,
    // so AppendEntries can be applied with prevLogIndex=0, prevLogTerm=0 when there's no
    // entries in storage.
    entries_.push_back(PBEntry().Index(0).Term(0).v);
  }

  explicit MemoryStorage(EntryVec vec) : MemoryStorage() {
    Append(vec);
  }

  // Compact discards all log entries prior to compactIndex.
  // It is the application's responsibility to not attempt to compact an index
  // greater than raftLog.applied.
  Status Compact(uint64_t compactIndex);

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

  void unsafeAppend(pb::Entry &entry);

 public:
  /// The following functions are for test only.

  std::vector<pb::Entry> &TEST_Entries() {
    return entries_;
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
