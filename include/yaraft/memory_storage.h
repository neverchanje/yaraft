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

#include <yaraft/errors.h>
#include <yaraft/idl_wrapper.h>
#include <yaraft/storage.h>

namespace yaraft {

// MemoryStorage implements the Storage interface backed by an
// in-memory array.
// Thread-safe.
class MemoryStorage : public Storage {
 public:
  // disallow copying
  MemoryStorage(const MemoryStorage &) = delete;
  MemoryStorage &operator=(const MemoryStorage &) = delete;

  // Creates an empty MemoryStorage.
  MemoryStorage() {
    // When starting from scratch populate the list with a dummy entry at term zero.
    ents_.push_back(idl::Entry());
  }

  // InitialState implements the Storage interface.
  error_s InitialState(idl::HardState *hardState,
                       idl::ConfState *confState) override {
    *hardState = hardState_;
    *confState = snapshot_.metadata_conf_state();
    return error_s::ok();
  }

  // SetHardState saves the current HardState.
  void SetHardState(idl::HardState st) {
    std::lock_guard<std::mutex> guard(mu_);
    hardState_ = std::move(st);
  }

  error_with<const_span<idl::Entry>> Entries(uint64_t lo, uint64_t hi,
                                             uint64_t maxSize) override;

  error_with<uint64_t> Term(uint64_t i) const override;

  error_with<uint64_t> LastIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return lastIndex();
  }

  error_with<uint64_t> FirstIndex() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return firstIndex();
  }

  error_with<idl::Snapshot> Snapshot() const override {
    std::lock_guard<std::mutex> guard(mu_);
    return snapshot_;
  }

  // ApplySnapshot overwrites the contents of this Storage object with
  // those of the given snapshot.
  error_s ApplySnapshot(idl::Snapshot snap);

  // CreateSnapshot makes a snapshot which can be retrieved with Snapshot() and
  // can be used to reconstruct the state at that point.
  // If any configuration changes have been made since the last compaction,
  // the result of the last ApplyConfChange must be passed in.
  error_with<idl::Snapshot> CreateSnapshot(uint64_t i, idl::ConfState *cs,
                                           string_view data);

  // Compact discards all log entries prior to compactIndex.
  // It is the application's responsibility to not attempt to compact an index
  // greater than raftLog.applied.
  error_s Compact(uint64_t compactIndex);

  // Append the new entries to storage.
  // TODO (xiangli): ensure the entries are continuous and
  // entries[0].Index > ms.entries[0].Index
  error_s Append(const_span<idl::Entry> entries);

 private:
  uint64_t firstIndex() const { return ents_[0].index() + 1; }

  uint64_t lastIndex() const { return ents_[0].index() + ents_.size() - 1; }

 private:
  friend class MemoryStorageTest;
  friend class RaftTest;

  // Protects access to all fields. Most methods of MemoryStorage are
  // run on the raft thread, but Append() is run on an application
  // thread.
  mutable std::mutex mu_;

  idl::HardState hardState_;
  idl::Snapshot snapshot_;

  // ents[i] has raft log position i+snapshot.Metadata.Index
  idl::EntryVec ents_;
};

using MemStoreUptr = std::unique_ptr<MemoryStorage>;
using MemStoreSptr = std::shared_ptr<MemoryStorage>;

}  // namespace yaraft
