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

#include <yaraft/logger.h>
#include <yaraft/span.h>

#include "memory_storage_impl.h"
#include "unstable.h"
#include "util.h"

namespace yaraft {

class RaftLog {
 public:
  explicit RaftLog(Storage *storage, uint64_t maxMsgSize = noLimit) {
    if (storage == nullptr) {
      RAFT_PANIC("storage must not be nil");
      return;
    }

    storage_.reset(storage);
    maxMsgSize_ = maxMsgSize;

    uint64_t firstIndex = storage_->FirstIndex().get_value();
    uint64_t lastIndex = storage_->LastIndex().get_value();
    unstable_.offset = lastIndex + 1;
    // Initialize our committed and applied pointers to the time of the last compaction.
    committed_ = firstIndex - 1;
    applied_ = firstIndex - 1;
  }

  std::string String() const {
    return fmt::sprintf(
        "committed=%d, applied=%d, unstable.offset=%d, len(unstable.Entries)=%d",
        committed_, applied_, unstable_.offset, unstable_.entries.size());
  }

  // maybeAppend returns (0, false) if the entries cannot be appended.
  // Otherwise, it returns (last index of new entries, true).
  std::pair<uint64_t, bool> maybeAppend(uint64_t index, uint64_t logTerm,
                                        uint64_t committed,
                                        idl::EntrySpan ents) {
    if (matchTerm(index, logTerm)) {
      uint64_t lastnewi = index + ents.size();
      uint64_t ci = findConflict(ents);
      if (ci == 0) {
      } else if (ci <= committed_) {
        RAFT_LOG(PANIC,
                 "entry %d conflict with committed entry [committed(%d)]", ci,
                 committed_);
      } else {
        uint64_t offset = index + 1;
        append(ents.SliceFrom(ci - offset));
      }
      commitTo(std::min(committed, lastnewi));
      return {lastnewi, true};
    }
    return {0, false};
  }

  uint64_t append(idl::EntrySpan ents) {
    if (ents.empty()) {
      return lastIndex();
    }
    uint64_t after = ents[0].index() - 1;
    if (after < committed_) {
      RAFT_LOG(PANIC, "after(%d) is out of range [committed(%d)]", after, committed_);
    }
    unstable_.truncateAndAppend(ents);
    return lastIndex();
  }

  // FindConflict finds the index of the conflict.
  // It returns the first pair of conflicting entries between the existing
  // entries and the given entries, if there are any.
  // If there is no conflicting entries, and the existing entries contains
  // all the given entries, zero will be returned.
  // If there is no conflicting entries, but the given entries contains new
  // entries, the index of the first new entry will be returned.
  // An entry is considered to be conflicting if it has the same index but
  // a different term.
  // The first entry MUST have an index equal to the argument 'from'.
  // The index of the given entries MUST be continuously increasing.
  uint64_t findConflict(idl::EntrySpan ents) const {
    for (auto &ne : ents) {
      if (!matchTerm(ne.index(), ne.term())) {
        if (ne.index() <= lastIndex()) {
          RAFT_LOG(INFO, "found conflict at index %d [existing term: %d, conflicting term: %d]",
                   ne.index(), zeroTermOnErrCompacted(term(ne.index())), ne.term());
        }
        return ne.index();
      }
    }
    return 0;
  }

  idl::EntrySpan unstableEntries() {
    if (unstable_.entries.empty()) {
      return {};
    }
    return unstable_.entries;
  }

  // nextEnts returns all the available entries for execution.
  // If applied is smaller than the index of snapshot, it returns all committed
  // entries after the index of snapshot.
  idl::EntryVec nextEnts() {
    uint64_t off = std::max(applied_ + 1, firstIndex());
    if (committed_ + 1 > off) {
      auto res = slice(off, committed_ + 1, maxMsgSize_);
      if (!res.is_ok()) {
        RAFT_LOG(PANIC, "unexpected error when getting unapplied entries (%s)",
                 res.get_error().description());
      }
      return std::move(res.get_value());
    }
    return {};
  }

  // hasNextEnts returns if there is any available entries for execution. This
  // is a fast check without heavy raftLog.slice() in raftLog.nextEnts().
  bool hasNextEnts() {
    uint64_t off = std::max(applied_ + 1, firstIndex());
    return committed_ + 1 > off;
  }

  error_with<idl::Snapshot> snapshot() const {
    if (unstable_.snapshot) {
      return *unstable_.snapshot;
    }
    return storage_->Snapshot();
  }

  uint64_t firstIndex() const {
    auto p = unstable_.maybeFirstIndex();
    uint64_t i = p.first;
    bool ok = p.second;
    if (ok) {
      return i;
    }

    auto res = storage_->FirstIndex();
    if (!res.is_ok()) {
      RAFT_PANIC(res.get_error());
    }
    return res.get_value();
  }

  uint64_t lastIndex() const {
    auto p = unstable_.maybeLastIndex();
    uint64_t i = p.first;
    bool ok = p.second;
    if (ok) {
      return i;
    }

    auto res = storage_->LastIndex();
    if (!res.is_ok()) {
      RAFT_LOG(PANIC, "%s", res.get_error().description());
    }
    return res.get_value();
  }

  void commitTo(uint64_t tocommit) {
    // never decrease commit
    if (committed_ < tocommit) {
      if (lastIndex() < tocommit) {
        RAFT_LOG(PANIC,
                 "tocommit(%d) is out of range [lastIndex(%d)]. Was the raft "
                 "log corrupted, truncated, or lost?",
                 tocommit, lastIndex());
      }
      committed_ = tocommit;
    }
  }

  void appliedTo(uint64_t i) {
    if (i == 0) {
      return;
    }
    if (committed_ < i || i < applied_) {
      RAFT_LOG(PANIC,
               "applied(%d) is out of range [prevApplied(%d), committed(%d)]",
               i, committed_, applied_);
    }
    applied_ = i;
  }

  void stableTo(uint64_t i, uint64_t t) { unstable_.stableTo(i, t); }

  void stableSnapTo(uint64_t i) {}

  uint64_t lastTerm() const {
    auto res = term(lastIndex());
    if (!res.is_ok()) {
      RAFT_LOG(PANIC, "unexpected error when getting the last term (%s)",
               res.get_error().description());
    }
    return res.get_value();
  }

  error_with<uint64_t> term(uint64_t i) const {
    // the valid term range is [index of dummy entry, last index]
    uint64_t dummyIndex = firstIndex() - 1;
    if (i < dummyIndex || i > lastIndex()) {
      // TODO: return an error instead?
      return 0;
    }

    auto p = unstable_.maybeTerm(i);
    if (p.second) {
      return p.first;
    }

    auto res = storage_->Term(i);
    if (res.is_ok()) {
      return res.get_value();
    }
    const error_s &err = res.get_error();
    if (err == ErrCompacted || err == ErrUnavailable) {
      return err;
    }
    RAFT_PANIC(err);
    __builtin_unreachable();
  }

  error_with<idl::EntryVec> entries(uint64_t i, uint64_t maxSize) const {
    if (i > lastIndex()) {
      return idl::EntryVec{};
    }
    return slice(i, lastIndex() + 1, maxSize);
  }

  // allEntries returns all entries in the log.
  idl::EntryVec allEntries() const {
    auto res = entries(firstIndex(), noLimit);
    if (res.is_ok()) {
      return std::move(res.get_value());
    }
    if (res.get_error() == ErrCompacted) {  // try again if there was a racing compaction
      return allEntries();
    }
    // TODO (xiangli): handle error?
    RAFT_PANIC(res.get_error());
    __builtin_unreachable();
  }

  // isUpToDate determines if the given (lastIndex,term) log is more up-to-date
  // by comparing the index and term of the last entries in the existing logs.
  // If the logs have last entries with different terms, then the log with the
  // later term is more up-to-date. If the logs end with the same term, then
  // whichever log has the larger lastIndex is more up-to-date. If the logs are
  // the same, the given log is up-to-date.
  bool isUpToDate(uint64_t index, uint64_t term) const {
    return (term > lastTerm()) || (term == lastTerm() && index >= lastIndex());
  }

  bool matchTerm(uint64_t i, uint64_t t) const {
    auto res = term(i);
    if (!res.is_ok()) {
      return false;
    }
    return t == res.get_value();
  }

  bool maybeCommit(uint64_t maxIndex, uint64_t t) {
    if (maxIndex > committed_ && zeroTermOnErrCompacted(term(maxIndex)) == t) {
      commitTo(maxIndex);
      return true;
    }
    return false;
  }

  void restore(idl::Snapshot &snap) {
    RAFT_LOG(INFO, "log [%s] starts to restore snapshot [index: %d, term: %d]",
             String(), snap.metadata_index(), snap.metadata_term());
    committed_ = snap.metadata_index();
    unstable_.restore(snap);
  }

  error_with<idl::EntryVec> slice(uint64_t lo, uint64_t hi, uint64_t maxSize) const {
    auto err = mustCheckOutOfBounds(lo, hi);
    if (!err.is_ok()) {
      return err;
    }

    if (lo == hi) {
      return idl::EntryVec{};
    }

    idl::EntryVec ents;
    if (lo < unstable_.offset) {
      auto res = storage_->Entries(lo, std::min(hi, unstable_.offset), maxSize);
      err = res.get_error();
      if (err == ErrCompacted) {
        return err;
      } else if (err == ErrUnavailable) {
        RAFT_LOG(PANIC, "entries[%d:%d) is unavailable from storage", lo, std::min(hi, unstable_.offset));
      } else if (!err.is_ok()) {
        RAFT_PANIC(err);
      }

      // check if ents has reached the size limitation
      idl::EntrySpan storedEnts = res.get_value();
      if (storedEnts.size() < std::min(hi, unstable_.offset) - lo) {
        return idl::EntryVec(storedEnts);
      }

      ents = idl::EntryVec(storedEnts);
    }
    if (hi > unstable_.offset) {
      idl::EntrySpan unstable =
          unstable_.slice(std::max(lo, unstable_.offset), hi);
      ents.Append(unstable);
    }
    return idl::EntryVec(limitSize(maxSize, ents));
  }

  error_s mustCheckOutOfBounds(uint64_t lo, uint64_t hi) const {
    if (lo > hi) {
      RAFT_LOG(PANIC, "invalid slice %d > %d", lo, hi);
    }
    uint64_t fi = firstIndex();
    if (lo < fi) {
      return ErrCompacted;
    }

    uint64_t length = lastIndex() + 1 - fi;
    if (lo < fi || hi > fi + length) {
      RAFT_LOG(PANIC, "slice[%d,%d) out of bound [%d,%d]", lo, hi, fi,
               lastIndex());
    }
    return error_s::ok();
  }

  static uint64_t zeroTermOnErrCompacted(error_with<uint64_t> res) {
    if (res.is_ok()) {
      return res.get_value();
    }
    if (res.get_error() == ErrCompacted) {
      return 0;
    }
    RAFT_LOG(PANIC, "unexpected error (%s)", res.get_error().description());
    return 0;
  }

  // test util
  std::string ltoa() const {
    auto s = fmt::sprintf("committed: %d, ", committed_);
    s += fmt::sprintf("applied:  %d, [", committed_);

    auto v = allEntries();
    for (int i = 0; i < v.size(); i++) {
      s += fmt::sprintf("#%d: %s, ", i, v[i]);
    }
    return s + "]";
  }

 private:
  friend class RaftLogTest;
  friend class RaftPaperTest;
  friend class RaftTest;
  friend class BaseTest;
  friend class Raft;
  friend class RawNode;

  // storage contains all stable entries since the last snapshot.
  std::unique_ptr<Storage> storage_;

  // unstable contains all unstable entries and snapshot.
  // they will be saved into storage.
  Unstable unstable_;

  /// The following variables are volatile states kept on all servers, as
  /// referenced in raft paper
  /// Figure 2.
  /// Invariant: commitIndex >= lastApplied.

  // committed is the highest log position that is known to be in
  // stable storage on a quorum of nodes.
  uint64_t committed_{0};
  // applied is the highest log position that the application has
  // been instructed to apply to its state machine.
  // Invariant: applied <= committed
  uint64_t applied_{0};

  uint64_t maxMsgSize_{0};
};

using RaftLogSPtr = std::shared_ptr<RaftLog>;

}  // namespace yaraft
