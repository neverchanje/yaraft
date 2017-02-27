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

#include <vector>

#include "raftpb.pb.h"

#include <boost/optional.hpp>
#include <glog/logging.h>

namespace yaraft {

struct Unstable {
  bool Empty() const {
    return entries.empty();
  }

  boost::optional<uint64_t> MaybeTerm(uint64_t index) const {
    if (!entries.empty()) {
      if (index >= FirstIndex() && index <= LastIndex()) {
        return boost::make_optional(entries[index - FirstIndex()].term());
      }
    }
    return boost::none;
  }

  uint64_t FirstIndex() const {
    DLOG_ASSERT(!entries.empty());
    return entries.begin()->index();
  }

  uint64_t LastIndex() const {
    DLOG_ASSERT(!entries.empty());
    return entries.rbegin()->index();
  }

  void StableTo(uint64_t i, uint64_t t) {}

  std::vector<pb::Entry> entries;
};

}  // namespace yaraft
