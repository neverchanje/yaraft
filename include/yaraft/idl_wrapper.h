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

#ifdef IDL_TYPE_PROTO
#include <yaraft/pb/raft.pb.h>
#include <memory>
#elif defined(IDL_TYPE_THRIFT)
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <yaraft/thrift/raft_types.h>
#endif

#include <yaraft/go_slice.h>
#include <sstream>

namespace yaraft {
namespace idl {

enum EntryType { EntryNormal = 0,
                 EntryConfChange = 1 };

enum MessageType {
  MsgHup = 0,
  MsgBeat = 1,
  MsgProp = 2,
  MsgApp = 3,
  MsgAppResp = 4,
  MsgVote = 5,
  MsgVoteResp = 6,
  MsgSnap = 7,
  MsgHeartbeat = 8,
  MsgHeartbeatResp = 9,
  MsgUnreachable = 10,
  MsgSnapStatus = 11,
  MsgCheckQuorum = 12,
  MsgTransferLeader = 13,
  MsgTimeoutNow = 14,
  MsgReadIndex = 15,
  MsgReadIndexResp = 16,
  MsgPreVote = 17,
  MsgPreVoteResp = 18,

  NUM_MSG_TYPES
};

enum ConfChangeType {
  ConfChangeAddNode = 0,
  ConfChangeRemoveNode = 1,
  ConfChangeUpdateNode = 2,
  ConfChangeAddLearnerNode = 3
};

inline bool IsLocalMsg(MessageType msgt) {
  switch (msgt) {
    case MsgHup:
    case MsgBeat:
    case MsgUnreachable:
    case MsgSnapStatus:
    case MsgCheckQuorum:
      return true;
    default:
      return false;
  }
}

inline bool IsResponseMsg(MessageType msgt) {
  switch (msgt) {
    case MsgAppResp:
    case MsgVoteResp:
    case MsgHeartbeatResp:
    case MsgUnreachable:
    case MsgPreVoteResp:
      return true;
    default:
      return false;
  }
}

#ifdef IDL_TYPE_PROTO

inline const std::string &MessageTypeToString(MessageType type) {
  return pb::MessageType_Name(pb::MessageType(type));
}

struct ConfState {
  pb::ConfState v;

  // nodes
  ConfState &nodes(std::vector<uint64_t> nodes) {
    for (uint64_t n : nodes) {
      v.mutable_nodes()->Add(n);
    }
    return *this;
  }
  std::vector<uint64_t> nodes() const {
    return std::vector<uint64_t>(v.nodes().begin(), v.nodes().end());
  }

  // learners
  ConfState &learners(std::vector<uint64_t> learners) {
    for (uint64_t n : learners) {
      v.mutable_learners()->Add(n);
    }
    return *this;
  }
  std::vector<uint64_t> learners() const {
    return std::vector<uint64_t>(v.learners().begin(), v.learners().end());
  }
};

struct Snapshot {
  std::shared_ptr<pb::Snapshot> v;

  // metadata.index
  Snapshot &metadata_index(uint64_t index) {
    v->mutable_metadata()->set_index(index);
    return *this;
  }
  uint64_t metadata_index() const { return v->metadata().index(); }

  // metadata.term
  Snapshot &metadata_term(uint64_t term) {
    v->mutable_metadata()->set_term(term);
    return *this;
  }
  uint64_t metadata_term() const { return v->metadata().term(); }

  // metadata.conf_state
  Snapshot &metadata_conf_state(ConfState cstate) {
    auto conf = new pb::ConfState;
    conf->Swap(&cstate.v);
    v->mutable_metadata()->set_allocated_conf_state(conf);
    return *this;
  }
  ConfState metadata_conf_state() const { return {v->metadata().conf_state()}; }
};

struct Entry {
  std::shared_ptr<pb::Entry> v;

  // term
  Entry &term(uint64_t t) {
    v->set_term(t);
    return *this;
  }
  uint64_t term() const { return v->term(); }

  // index
  Entry &index(uint64_t index) {
    v->set_index(index);
    return *this;
  }
  uint64_t index() const { return v->index(); }

  // type
  Entry &type(EntryType type) {
    v->set_type((pb::EntryType)type);
    return *this;
  }
  EntryType type() const { return (EntryType)v->type(); }

  // data
  Entry &data(std::string data) {
    auto s = new std::string(std::move(data));
    v->set_allocated_data(s);
    return *this;
  }
  std::string &data() const { return *v->mutable_data(); }

  Entry() : v(new pb::Entry) {}
};

typedef GoSlice<Entry> EntryVec;

// Constant view over EntryVec.
typedef span<Entry> EntrySpan;

// Message is move constructible only.
struct Message {
  std::shared_ptr<pb::Message> v;

  // type
  Message &type(MessageType type) {
    v->set_type((pb::MessageType)type);
    return *this;
  }
  MessageType type() const { return (MessageType)v->type(); }

  // to
  Message &to(uint64_t to) {
    v->set_to(to);
    return *this;
  }
  uint64_t to() const { return v->to(); }

  // from
  Message &from(uint64_t from) {
    v->set_from(from);
    return *this;
  }
  uint64_t from() const { return v->from(); }

  // term
  Message &term(uint64_t term) {
    v->set_term(term);
    return *this;
  }
  uint64_t term() const { return v->term(); }

  // logterm
  Message &logterm(uint64_t logTerm) {
    v->set_logterm(logTerm);
    return *this;
  }
  uint64_t logterm() const { return v->logterm(); }

  // index
  Message &index(uint64_t index) {
    v->set_index(index);
    return *this;
  }
  uint64_t index() const { return v->index(); }

  // commit
  Message &commit(uint64_t commit) {
    v->set_commit(commit);
    return *this;
  }
  uint64_t commit() const { return v->commit(); }

  Message &snapshot(Snapshot &&snap) {
    auto s = new pb::Snapshot();
    s->Swap(&snap.v);
    v->set_allocated_snapshot(s);
    return *this;
  }

  // reject
  Message &reject(bool reject) {
    v->set_reject(reject);
    return *this;
  }
  bool reject() const { return v->reject(); }

  // rejecthint
  Message &rejecthint(uint64_t hint) {
    v->set_rejecthint(hint);
    return *this;
  }
  uint64_t rejecthint() const { return v->rejecthint(); }

  // entries
  Message &entries(EntryVec list) {
    for (Entry &e : list) {
      v->add_entries()->Swap(&(*e.v));
    }
    return *this;
  }
  EntrySpan entries() const {
    return v->entries();
  }

  // context
  Message &context(std::string ctx) {
    v->set_allocated_context(new std::string(std::move(ctx)));
    return *this;
  }
  const std::string &context() const {
    return v->context();
  }

  Message() : v(new pb::Message) {}
};

struct HardState {
  pb::HardState v;

  // vote
  HardState &vote(uint64_t vote) {
    v.set_vote(vote);
    return *this;
  }
  uint64_t vote() const { return v.vote(); }

  // term
  HardState &term(uint64_t t) {
    v.set_term(t);
    return *this;
  }
  uint64_t term() const { return v.term(); }

  // commit
  HardState &commit(uint64_t commit) {
    v.set_commit(commit);
    return *this;
  }
  uint64_t commit() const { return v.commit(); }
};

struct ConfChange {
  pb::ConfChange v;

  // ID
  ConfChange &id(uint64_t id) {
    v.set_id(id);
    return *this;
  }
  uint64_t id() const { return v.id(); }

  // type
  ConfChange &type(ConfChangeType type) {
    v.set_type((pb::ConfChangeType)type);
    return *this;
  }
  ConfChangeType type() const { return (ConfChangeType)v.type(); }

  // NodeId
  ConfChange &nodeid(uint64_t id) {
    v.set_nodeid(id);
    return *this;
  }
  uint64_t nodeid() const { return v.nodeid(); }

  std::string ToString() const { return v.SerializeAsString(); }
};

#elif IDL_TYPE_THRIFT

inline std::ostream &operator<<(std::ostream &os, MessageType type) {
  auto it = thrift::_MessageType_VALUES_TO_NAMES.find(type);
  return os << it->second;
}

struct ConfState {
  thrift::ConfState v;

  // nodes
  ConfState &nodes(std::vector<uint64_t> nodes) {
    v.nodes.clear();
    for (uint64_t n : nodes) {
      v.nodes.push_back(static_cast<int64_t>(n));
    }
    return *this;
  }
  std::vector<uint64_t> nodes() const {
    std::vector<uint64_t> ret;
    for (int64_t n : v.nodes) {
      ret.push_back(static_cast<uint64_t>(n));
    }
    return ret;
  }

  // learners
  ConfState &learners(std::vector<uint64_t> learners) {
    v.learners.clear();
    for (uint64_t n : learners) {
      v.learners.push_back(static_cast<int64_t>(n));
    }
    return *this;
  }
  std::vector<uint64_t> learners() const {
    std::vector<uint64_t> ret;
    for (int64_t n : v.learners) {
      ret.push_back(static_cast<uint64_t>(n));
    }
    return ret;
  }
};

struct Snapshot {
  std::shared_ptr<thrift::Snapshot> v;

  // metadata.index
  Snapshot &metadata_index(uint64_t index) {
    v->metadata.__set_index(index);
    return *this;
  }
  uint64_t metadata_index() const { return static_cast<uint64_t>(v->metadata.index); }

  // metadata.term
  Snapshot &metadata_term(uint64_t term) {
    v->metadata.__set_term(term);
    return *this;
  }
  uint64_t metadata_term() const { return static_cast<uint64_t>(v->metadata.term); }

  // metadata.conf_state
  Snapshot &metadata_conf_state(ConfState cstate) {
    v->metadata.conf_state = std::move(cstate.v);
    return *this;
  }
  ConfState metadata_conf_state() const { return {v->metadata.conf_state}; }

  friend std::ostream &operator<<(std::ostream &os, const Snapshot &s) {
    return os << *s.v;
  }
  friend bool operator==(const Snapshot &a, const Snapshot &b) {
    std::ostringstream os1, os2;
    os1 << a;
    os2 << b;
    return os1.str() == os2.str();
  }

  Snapshot() : v(new thrift::Snapshot()) {}

  // holds a shallow reference to the Snapshot.
  explicit Snapshot(thrift::Snapshot *e)
      : v(e, [](thrift::Snapshot *) {}) {
  }
};

struct Entry {
  std::shared_ptr<thrift::Entry> v;

  // term
  Entry &term(uint64_t t) {
    v->__set_Term(t);
    return *this;
  }
  uint64_t term() const { return static_cast<uint64_t>(v->Term); }

  // index
  Entry &index(uint64_t index) {
    v->__set_Index(index);
    return *this;
  }
  uint64_t index() const { return static_cast<uint64_t>(v->Index); }

  // type
  Entry &type(EntryType type) {
    v->__set_Type((thrift::EntryType::type)type);
    return *this;
  }
  EntryType type() const { return (EntryType)v->Type; }

  // data
  Entry &data(std::string data) {
    v->Data = std::move(data);
    return *this;
  }
  std::string &data() const { return v->Data; }

  size_t ByteSize() const {
    return 10 + v->Data.size();
  }

  friend std::ostream &operator<<(std::ostream &os, const Entry &m) {
    m.v->printTo(os);
    return os;
  }

  Entry() : v(new thrift::Entry) {}

  // holds a shallow reference to the Entry.
  explicit Entry(thrift::Entry *e)
      : v(e, [](thrift::Entry *) {}) {
  }
};

typedef GoSlice<Entry> EntryVec;

// Constant view over EntryVec.
typedef const_span<Entry> EntrySpan;

struct Message {
  std::shared_ptr<thrift::Message> v;
  // set v->entries only when this message is marshaled.
  std::shared_ptr<EntryVec> ventries;

  // type
  Message &type(MessageType type) {
    v->__set_type((thrift::MessageType::type)type);
    return *this;
  }
  MessageType type() const { return (MessageType)v->type; }

  // to
  Message &to(uint64_t to) {
    v->__set_to(to);
    return *this;
  }
  uint64_t to() const { return static_cast<uint64_t>(v->to); }

  // from
  Message &from(uint64_t from) {
    v->__set_nfrom(from);
    return *this;
  }
  uint64_t from() const { return static_cast<uint64_t>(v->nfrom); }

  // term
  Message &term(uint64_t term) {
    v->__set_term(term);
    return *this;
  }
  uint64_t term() const { return static_cast<uint64_t>(v->term); }

  // logterm
  Message &logterm(uint64_t logTerm) {
    v->__set_logTerm(logTerm);
    return *this;
  }
  uint64_t logterm() const { return static_cast<uint64_t>(v->logTerm); }

  // index
  Message &index(uint64_t index) {
    v->__set_index(index);
    return *this;
  }
  uint64_t index() const { return static_cast<uint64_t>(v->index); }

  // commit
  Message &commit(uint64_t commit) {
    v->__set_commit(commit);
    return *this;
  }
  uint64_t commit() const { return static_cast<uint64_t>(v->commit); }

  // snapshot
  Message &snapshot(Snapshot snap) {
    v->snapshot = std::move(*snap.v);
    return *this;
  }
  Snapshot snapshot() { return Snapshot(&v->snapshot); }

  // reject
  Message &reject(bool reject) {
    v->__set_reject(reject);
    return *this;
  }
  bool reject() const { return v->reject; }

  // rejecthint
  Message &rejecthint(uint64_t hint) {
    v->__set_rejectHint(hint);
    return *this;
  }
  uint64_t rejecthint() const { return static_cast<uint64_t>(v->rejectHint); }

  // entries
  Message &entries(EntryVec list) {
    ventries.reset(new EntryVec(std::move(list)));
    return *this;
  }
  EntryVec &entries() {
    return *ventries;
  }
  const EntryVec &entries() const {
    return *ventries;
  }

  // context
  Message &context(std::string ctx) {
    v->context = std::move(ctx);
    return *this;
  }
  const std::string &context() const {
    return v->context;
  }

  friend std::ostream &operator<<(std::ostream &os, const Message &msg) {
    auto m = const_cast<Message &>(msg);
    if (m.v->entries.empty()) {
      for (auto &e : *m.ventries) {
        m.v->entries.push_back(*e.v);
      }
    }
    m.v->printTo(os);
    return os;
  }
  friend bool operator<(const Message &a, const Message &b) {
    return a.ToString() < b.ToString();
  }
  friend bool operator==(const Message &a, const Message &b) {
    return a.ToString() == b.ToString();
  }
  std::string ToString() const {
    std::ostringstream os;
    os << *this;
    return os.str();
  }
  Message DeepClone() const {
    Message c;
    *c.v = *v;
    for (auto &e : *ventries) {
      Entry newe;
      *newe.v = *e.v;
      c.ventries->push_back(newe);
    }
    return c;
  }

  Message() : v(new thrift::Message), ventries(new EntryVec) {}
};

struct HardState {
  thrift::HardState v;

  // vote
  HardState &vote(uint64_t vote) {
    v.__set_vote(vote);
    return *this;
  }
  uint64_t vote() const { return static_cast<uint64_t>(v.vote); }

  // term
  HardState &term(uint64_t t) {
    v.__set_term(t);
    return *this;
  }
  uint64_t term() const { return static_cast<uint64_t>(v.term); }

  // commit
  HardState &commit(uint64_t commit) {
    v.__set_commit(commit);
    return *this;
  }
  uint64_t commit() const { return static_cast<uint64_t>(v.commit); }

  friend std::ostream &operator<<(std::ostream &os, const HardState &s) {
    return os << s.v;
  }
  friend bool operator==(const HardState &a, const HardState &b) {
    std::ostringstream os1, os2;
    os1 << a;
    os2 << b;
    return os1.str() == os2.str();
  }
};

struct ConfChange {
  thrift::ConfChange v;

  // ID
  ConfChange &id(uint64_t id) {
    v.__set_ID(id);
    return *this;
  }
  uint64_t id() const { return static_cast<uint64_t>(v.ID); }

  // type
  ConfChange &type(ConfChangeType type) {
    v.__set_Type((thrift::ConfChangeType::type)type);
    return *this;
  }
  ConfChangeType type() const { return (ConfChangeType)v.Type; }

  // NodeId
  ConfChange &nodeid(uint64_t id) {
    v.__set_NodeID(static_cast<int64_t>(id));
    return *this;
  }
  uint64_t nodeid() const { return static_cast<uint64_t>(v.NodeID); }

  // context
  ConfChange &context(const std::string &ctx) {
    v.__set_Context(ctx);
    return *this;
  }
  std::string context() const { return v.Context; }

  std::string Marshal() const {
    using namespace apache::thrift::transport;
    using namespace apache::thrift::protocol;
    boost::shared_ptr<TMemoryBuffer> buf(new TMemoryBuffer());
    TBinaryProtocol protocol(buf);
    v.write(&protocol);
    return buf->getBufferAsString();
  }

  void Unmarshal(const std::string &data) {
    using namespace apache::thrift::transport;
    using namespace apache::thrift::protocol;
    boost::shared_ptr<TMemoryBuffer> buf(new TMemoryBuffer());
    buf->write(reinterpret_cast<const uint8_t *>(data.data()), static_cast<uint32_t>(data.size()));
    TBinaryProtocol protocol(buf);
    v.read(&protocol);
  }
};

#else
#error "unsupported idl type"
#endif

inline bool isHardStateEqual(const HardState &a, const HardState &b) {
  return a.term() == b.term() && a.vote() == b.vote() && a.commit() == b.commit();
}

inline bool IsEmptyHardState(const HardState &a) {
  static HardState emptyState;
  return isHardStateEqual(a, emptyState);
}

// IsEmptySnap returns true if the given Snapshot is empty
inline bool IsEmptySnap(const Snapshot &sp) {
  return sp.metadata_index() == 0;
}

}  // namespace idl
}  // namespace yaraft
