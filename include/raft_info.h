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

#include <cstdint>
#include <unordered_map>

namespace yaraft {

struct RaftProgress {
  RaftProgress(uint64_t next, uint64_t match) : nextIndex(next), matchIndex(match) {}
  RaftProgress(): nextIndex(0), matchIndex(0) {}

  uint64_t nextIndex;
  uint64_t matchIndex;
};

struct RaftInfo {
  uint64_t currentLeader;
  uint64_t currentTerm;
  uint64_t logIndex;
  uint64_t commitIndex;
  std::unordered_map<uint64_t, RaftProgress> progress;
};

}  // namespace yaraft
