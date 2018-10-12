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

#include <yaraft/logger.h>
#include <yaraft/span.h>

#include "exception.h"
#include "memory_storage_impl.h"
#include "util.h"

namespace yaraft {

error_with<idl::EntrySpan> MemoryStorage::Entries(uint64_t lo, uint64_t hi,
                                                  uint64_t maxSize) {
  std::lock_guard<std::mutex> guard(mu_);

  uint64_t offset = ents_[0].index();
  if (lo <= offset) {
    return ErrCompacted;
  }
  if (hi > lastIndex() + 1) {
    RAFT_LOG(PANIC, "entries' hi(%d) is out of bound lastindex(%d)", hi,
             lastIndex());
  }
  // only contains dummy entries
  if (ents_.size() == 1) {
    return ErrUnavailable;
  }

  // ents := ms.ents[lo-offset : hi-offset]
  idl::EntrySpan ents = ents_.View().Slice(lo - offset, hi - offset);
  return limitSize(maxSize, ents);
}

error_with<uint64_t> MemoryStorage::Term(uint64_t i) const {
  std::lock_guard<std::mutex> guard(mu_);

  uint64_t offset = ents_[0].index();
  if (i < offset) {
    return ErrCompacted;
  }
  if (i - offset >= ents_.size()) {
    return ErrUnavailable;
  }
  return ents_[i - offset].term();
}

error_s MemoryStorage::ApplySnapshot(idl::Snapshot snap) {
  std::lock_guard<std::mutex> guard(mu_);

  auto msIndex = snapshot_.metadata_index();
  auto snapIndex = snap.metadata_index();
  if (msIndex >= snapIndex) {
    return ErrSnapOutOfDate;
  }

  ents_ = {};
  ents_.push_back(
      idl::Entry().term(snap.metadata_term()).index(snap.metadata_index()));
  snapshot_ = std::move(snap);
  return error_s::ok();
}

error_with<idl::Snapshot> MemoryStorage::CreateSnapshot(uint64_t i,
                                                        idl::ConfState *cs,
                                                        std::string data) {
  std::lock_guard<std::mutex> guard(mu_);
  if (i <= snapshot_.metadata_index()) {
    return ErrSnapOutOfDate;
  }

  auto offset = ents_[0].index();
  if (i > lastIndex()) {
    RAFT_LOG(PANIC, "snapshot %d is out of bound lastindex(%d)", i,
             lastIndex());
  }

  snapshot_.metadata_index(i);
  snapshot_.metadata_term(ents_[i - offset].term());
  if (cs != nullptr) {
    snapshot_.metadata_conf_state(*cs);
  }
  snapshot_.data(std::move(data));
  return snapshot_;
}

error_s MemoryStorage::Compact(uint64_t compactIndex) {
  std::lock_guard<std::mutex> guard(mu_);

  auto offset = ents_[0].index();
  if (compactIndex <= offset) {
    return ErrCompacted;
  }
  if (compactIndex > lastIndex()) {
    RAFT_LOG(PANIC, "compact %d is out of bound lastindex(%d)", compactIndex, lastIndex());
  }

  size_t i = compactIndex - offset;
  auto ents = idl::EntryVec::make(1, 1 + ents_.size() - i);
  ents[0].index(ents_[i].index());
  ents[0].term(ents_[i].term());
  // ents = append(ents, ms.ents[i+1:]...)
  ents_.SliceFrom(i + 1);
  ents.Append(ents_);
  ents_ = std::move(ents);
  return error_s::ok();
}

error_s MemoryStorage::Append(idl::EntrySpan entries) {
  if (entries.empty())
    return error_s::ok();

  std::lock_guard<std::mutex> guard(mu_);

  auto first = firstIndex();
  auto last = entries[0].index() + entries.size() - 1;

  // shortcut if there is no new entry.
  if (last < first) {
    return error_s::ok();
  }
  // truncate compacted entries
  if (first > entries[0].index()) {
    //  entries = entries[first-entries[0].Index:]
    entries = entries.SliceFrom(first - entries[0].index());
  }

  auto offset = entries[0].index() - ents_[0].index();
  if (ents_.size() > offset) {
    // ms.ents = append([]pb.Entry{}, ms.ents[:offset]...)
    // ms.ents = append(ms.ents, entries...)
    ents_.SliceTo(offset);
    ents_.Append(entries);
  } else if (ents_.size() == offset) {
    // ms.ents = append(ms.ents, entries...)
    ents_.Append(entries);
  } else {
    RAFT_LOG(PANIC, "missing log entry [last: %d, append at: %d]",
             lastIndex(), entries[0].index());
  }
  return error_s::ok();
}

}  // namespace yaraft
