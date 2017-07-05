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
  return a.term() == b.term() && a.vote() == b.vote();
}

bool operator!=(const pb::HardState& a, const pb::HardState& b) {
  return !(a == b);
}

RawNode::RawNode(Config* conf) : raft_(new Raft(conf)), prevHardState_(new pb::HardState) {}

RawNode::~RawNode() {
  delete raft_;
}

void RawNode::Tick() {
  raft_->Tick();
}

Status RawNode::Step(pb::Message& m) {
  // ignore unexpected local messages receiving over network
  if (IsLocalMsg(m)) {
    return Status::Make(Error::StepLocalMsg, "cannot step raft local message");
  }

  if (!raft_->HasPeer(m.from()) && IsResponseMsg(m)) {
    return Status::Make(Error::StepPeerNotFound,
                        "cannot step a response message from peer not found");
  }

  return raft_->Step(m);
}

Status RawNode::Campaign() {
  uint64_t id = raft_->Id(), term = raft_->Term();
  return raft_->Step(PBMessage().From(id).To(id).Type(pb::MsgHup).Term(term).v);
}

Ready* RawNode::GetReady() {
  std::unique_ptr<Ready> rd(new Ready);
  rd->entries = &raft_->log_->GetUnstable().entries;
  rd->messages = std::move(raft_->mails_);

  pb::HardState hs = PBHardState().Vote(raft_->votedFor_).Term(raft_->currentTerm_).v;
  if (!prevHardState_->IsInitialized() || (*prevHardState_) != hs) {
    rd->hardState.reset(new pb::HardState(hs));
    *prevHardState_ = hs;
  }

  // return null if Ready is empty
  if (IsReadyEmpty(*rd)) {
    return nullptr;
  }

  return rd.release();
}

void RawNode::Advance(const Ready& ready) {
  // stable the unstable entries to memory storage.
  MemoryStorage* storage = static_cast<MemoryStorage*>(raft_->log_->GetStorage());
  storage->Append(std::move(raft_->log_->GetUnstable().entries));
}

RaftInfo RawNode::GetInfo() const {
  RaftInfo info;
  info.logIndex = raft_->log_->LastIndex();
  info.currentLeader = raft_->currentLeader_;
  info.currentTerm = raft_->currentTerm_;
  return info;
}

}  // namespace yaraft
