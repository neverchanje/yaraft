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

#include "raw_node.h"
#include "conf.h"
#include "fluent_pb.h"
#include "memory_storage.h"
#include "raft.h"
#include "ready.h"

namespace yaraft {

bool operator==(const pb::HardState& a, const pb::HardState& b) {
#define COMPARE_PROTO_OPTIONAL_FIELD(msg1, msg2, field)                                 \
  ((msg1).has_##field() && (msg2).has_##field() && (msg1).field() == (msg2).field()) || \
      (!(msg1).has_##field() && !(msg2).has_##field())

  if (!COMPARE_PROTO_OPTIONAL_FIELD(a, b, commit))
    return false;
  if (!COMPARE_PROTO_OPTIONAL_FIELD(a, b, vote))
    return false;
  if (!COMPARE_PROTO_OPTIONAL_FIELD(a, b, term))
    return false;

#undef COMPARE_PROTO_OPTIONAL_FIELD

  return true;
}

bool operator!=(const pb::HardState& a, const pb::HardState& b) {
  return !(a == b);
}

RawNode::RawNode(Config* conf) : prevHardState_(new pb::HardState) {
  // validate first to avoid bad config (which may cause crazy segfault).
  FATAL_NOT_OK(conf->Validate(), "Config::Validate");

  raft_.reset(new Raft(conf));
}

RawNode::~RawNode() = default;

void RawNode::Tick() {
  raft_->Tick();
}

Status RawNode::Step(pb::Message& m) {
  // ignore unexpected local messages receiving over network
  if (IsLocalMessage(m.type())) {
    return Status::Make(Error::StepLocalMsg, "cannot step raft local message");
  }

  if (!raft_->HasPeer(m.from()) && IsResponseMsg(m)) {
    return Status::Make(Error::StepPeerNotFound,
                        "cannot step a response message from peer not found");
  }

  return raft_->Step(m);
}

Status RawNode::Propose(const silly::Slice& data) {
  if (!IsLeader()) {
    return Status::Make(Error::ProposeToNonLeader, fmt::format("{} is not leader", raft_->Id()));
  }

  uint64_t id = raft_->Id(), term = raft_->Term();
  auto e = PBEntry().Data(data).v;
  return raft_->Step(PBMessage().From(id).To(id).Type(pb::MsgProp).Term(term).Entries({e}).v);
}

Status RawNode::Campaign() {
  uint64_t id = raft_->Id(), term = raft_->Term();
  return raft_->Step(PBMessage().From(id).To(id).Type(pb::MsgHup).Term(term).v);
}

Ready* RawNode::GetReady() {
  std::unique_ptr<Ready> rd(new Ready);
  rd->entries = std::move(raft_->log_->GetUnstable().entries);
  rd->messages = std::move(raft_->mails_);

  pb::HardState hs = PBHardState()
                         .Vote(raft_->votedFor_)
                         .Term(raft_->currentTerm_)
                         .Commit(raft_->log_->CommitIndex())
                         .v;
  if (!prevHardState_->IsInitialized() || (*prevHardState_) != hs) {
    rd->hardState.reset(new pb::HardState(hs));
    *prevHardState_ = hs;
  }

  // return null if Ready is empty
  if (rd->IsEmpty()) {
    return nullptr;
  }

  return rd.release();
}

void RawNode::ReportSnapshot(uint64_t id, RawNode::SnapshotStatus status) {
  bool reject = status == kSnapshotFailure;
  raft_->Step(PBMessage().Type(pb::MsgSnapStatus).From(id).To(id).Reject(reject).v);
}

void RawNode::ReportUnreachable(uint64_t id) {
  raft_->Step(PBMessage().Type(pb::MsgUnreachable).From(id).To(id).v);
}

pb::ConfState RawNode::ApplyConfChange(const pb::ConfChange& cc) {
  pb::ConfState confState;
  if (!cc.has_nodeid() || cc.nodeid() == 0) {
    FMT_LOG(FATAL, "what???");
  }

  switch (cc.type()) {
    case pb::ConfChangeAddNode:
      raft_->AddNode(cc.nodeid());
      break;
    case pb::ConfChangeRemoveNode:
      raft_->RemoveNode(cc.nodeid());
      break;
    default:
      FMT_LOG(FATAL, "unexpected conf type");
  }

  confState.add_nodes(cc.nodeid());
  return confState;
}

Status RawNode::ProposeConfChange(const pb::ConfChange& cc) {
  if (!IsLeader()) {
    return Status::Make(Error::ProposeToNonLeader, fmt::format("{} is not leader", raft_->Id()));
  }

  return raft_->Step(
      PBMessage()
          .From(1)
          .To(1)
          .Type(pb::MsgProp)
          .Entries({PBEntry().Type(pb::EntryConfChange).Data(cc.SerializeAsString()).v})
          .v);
}

uint64_t RawNode::Id() const {
  return raft_->Id();
}

uint64_t RawNode::CurrentTerm() const {
  return raft_->Term();
}

uint64_t RawNode::CommittedIndex() const {
  return raft_->log_->CommitIndex();
}

uint64_t RawNode::LastIndex() const {
  return raft_->log_->LastIndex();
}

uint64_t RawNode::LeaderHint() const {
  return raft_->currentLeader_;
}

bool RawNode::IsLeader() const {
  return raft_->id_ == raft_->currentLeader_;
}

}  // namespace yaraft
