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

#include "memory_storage.h"
#include "exception.h"

#include <glog/logging.h>

namespace yaraft {

StatusWith<uint64_t> MemoryStorage::Term(uint64_t i) const {
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

StatusWith<EntryVec> MemoryStorage::Entries(uint64_t lo, uint64_t hi, uint64_t *maxSize) {
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
    auto &e = entries_[i + loOffset];
    size += e.ByteSize();
    if (size > *maxSize) {
      size -= e.ByteSize();
      break;
    }
    ret.push_back(e);
  }
  *maxSize -= size;
  return ret;
}

Status MemoryStorage::Compact(uint64_t compactIndex) {
  uint64_t beginIndex = entries_.begin()->index();
  if (compactIndex <= beginIndex) {
    return Status::Make(Error::LogCompacted);
  }

  if (compactIndex > lastIndex()) {
#ifdef BUILD_TESTS
    throw RaftError("compact {} is out of bound lastindex({})", compactIndex,
                    LastIndex().GetValue());
#else
    FMT_LOG(FATAL, "compact {} is out of bound lastindex({})", compactIndex,
            LastIndex().GetValue());
#endif
  }

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

void MemoryStorage::unsafeAppend(pb::Entry &entry) {
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

}  // namespace yaraft
