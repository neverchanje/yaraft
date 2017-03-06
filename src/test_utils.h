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

#include "conf.h"
#include "raft.h"
#include "raftpb.pb.h"
#include "storage.h"

namespace yaraft {

class Network {
 public:
  Network(Config* cfg) {}

  void Send(const pb::Message& msg) {
    msg_.push_back(msg);
  }

 private:
  std::vector<pb::Message> msg_;
};

Config* newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                      Storage* storage) {
  auto conf = new Config();
  conf->id = id;
  conf->electionTick = election;
  conf->heartbeatTick = heartbeat;
  conf->storage = storage;
  conf->peers = std::move(peers);
  return conf;
}

Raft* newTestRaft(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                  Storage* storage) {
  return Raft::Create(newTestConfig(id, peers, election, heartbeat, storage));
}

pb::Entry pbEntry(uint64_t index, uint64_t term) {
  pb::Entry tmp;
  tmp.set_term(term);
  tmp.set_index(index);
  return tmp;
}

EntryVec& operator<<(EntryVec& v, const pb::Entry& e) {
  v.push_back(e);
  return v;
}

EntryVec& operator<<(EntryVec& v, const EntryVec& v2) {
  for (const auto& e : v2)
    v << e;
  return v;
}

EntryVec operator+(EntryVec v, pb::Entry e) {
  v.push_back(e);
  return v;
}

EntryVec operator+(pb::Entry e1, pb::Entry e2) {
  EntryVec v;
  v.push_back(e1);
  v.push_back(e2);
  return v;
}

inline bool operator==(pb::Entry e1, pb::Entry e2) {
  bool result = (e1.term() == e2.term()) && (e1.index() == e2.index());
  return result;
}

inline bool operator!=(pb::Entry e1, pb::Entry e2) {
  return !(e1 == e2);
}

inline bool operator==(EntryVec v1, EntryVec v2) {
  if (v1.size() != v2.size())
    return false;
  auto it1 = v1.begin();
  auto it2 = v2.begin();
  while (it1 != v1.end()) {
    if (*it1++ != *it2++)
      return false;
  }
  return true;
}

inline std::ostream& operator<<(std::ostream& os, const pb::Entry& e) {
  return os << "{Index: " << e.index() << ", Term: " << e.term() << "}";
}

inline std::ostream& operator<<(std::ostream& os, const EntryVec& v) {
  os << "Size of entry vec: " << v.size() << ". | ";
  for (const auto& e : v) {
    os << e << " | ";
  }
  return os << "\n";
}

uint64_t mustTerm(const RaftLog& log, uint64_t index) {
  auto s = log.Term(index);
  return s.OK() ? s.GetValue() : 0;
}

}  // namespace yaraft
