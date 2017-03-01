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

#include "raftpb.pb.h"

namespace yaraft {

template <typename T>
struct Compose {};

template <>
struct Compose<pb::Message> {
  pb::Message value;

  Compose& Term(uint64_t term) {
    value.set_term(term);
    return *this;
  }

  Compose& LogTerm(uint64_t logTerm) {
    value.set_logterm(logTerm);
    return *this;
  }

  Compose& Type(pb::MessageType type) {
    value.set_type(type);
    return *this;
  }

  Compose& To(uint64_t to) {
    value.set_to(to);
    return *this;
  }

  Compose& From(uint64_t from) {
    value.set_from(from);
    return *this;
  }

  Compose& Index(uint64_t index) {
    value.set_index(index);
    return *this;
  }

  Compose& Commit(uint64_t commit) {
    value.set_commit(commit);
    return *this;
  }
};

using PBMessage = Compose<pb::Message>;

template <>
struct Compose<pb::Entry> {
  pb::Entry value;

  Compose& Type(pb::EntryType type) {
    value.set_type(type);
    return *this;
  }

  Compose& Index(uint64_t index) {
    value.set_index(index);
    return *this;
  }

  Compose& Term(uint64_t term) {
    value.set_term(term);
    return *this;
  }
};

using PBEntry = Compose<pb::Entry>;

}  // namespace yaraft
