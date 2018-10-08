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

#include <yaraft/conf.h>
#include <yaraft/memory_storage.h>
#include <yaraft/raw_node.h>
#include <yaraft/ready.h>

#include "raft.h"

namespace yaraft {

idl::HardState emptyState;

RawNode *RawNode::New(Config *config, std::vector<Peer> peers) {
  if (config->id == 0) {
    RAFT_LOG(PANIC, "config.ID must not be zero");
  }

  auto errWithLastIndex = config->storage->LastIndex();
  if (!errWithLastIndex.is_ok()) {
    RAFT_PANIC(errWithLastIndex.get_error());
  }
  uint64_t lastIndex = errWithLastIndex.get_value();

  auto r = Raft::New(config);
  auto rn = new RawNode;
  rn->raft_.reset(r);

  // If the log is empty, this is a new RawNode (like StartNode); otherwise it's
  // restoring an existing RawNode (like RestartNode).
  // TODO(bdarnell): rethink RawNode initialization and whether the application needs
  // to be able to tell us when it expects the RawNode to exist.
  if (lastIndex == 0) {
    r->becomeFollower(1, kNone);
    auto ents = idl::EntryVec::make(peers.size());
    for (uint64_t i = 0; i < peers.size(); i++) {
      auto peer = peers[i];
      auto cc = idl::ConfChange().type(idl::ConfChangeAddNode).nodeid(peer.ID).context(peer.Context);
      auto data = cc.Marshal();
      ents[i] = idl::Entry().type(idl::EntryConfChange).term(1).index(i + 1).data(std::move(data));
    }
    r->raftLog_->append(ents);
    r->raftLog_->committed_ = ents.size();
    for (auto peer : peers) {
      r->addNode(peer.ID);
    }
  }

  // Set the initial hard and soft states after performing all initialization.
  rn->prevSoftSt_ = r->softState();

  if (lastIndex == 0) {
    rn->prevHardSt_ = emptyState;
  } else {
    rn->prevHardSt_ = r->hardState();
  }

  return rn;
}

RawNode::~RawNode() = default;

void RawNode::Tick() { raft_->tick(); }

error_s RawNode::Campaign() {
  return raft_->Step(idl::Message().type(idl::MsgHup));
}

error_s RawNode::Propose(std::string &&data) {
  return raft_->Step(idl::Message()
                         .type(idl::MsgProp)
                         .from(raft_->id_)
                         .entries({idl::Entry().data(std::move(data))}));
}

error_s RawNode::ProposeConfChange(idl::ConfChange cc) {
  return raft_->Step(
      idl::Message()
          .type(idl::MsgProp)
          .entries(
              {idl::Entry().type(idl::EntryConfChange).data(cc.Marshal())}));
}

idl::ConfState RawNode::ApplyConfChange(idl::ConfChange cc) {
  if (cc.nodeid() == kNone) {
    return idl::ConfState().nodes(raft_->nodes()).learners(raft_->learnerNodes());
  }

  switch (cc.type()) {
    case idl::ConfChangeAddNode:
      raft_->addNode(cc.nodeid());
      break;
    case idl::ConfChangeAddLearnerNode:
      raft_->addLearner(cc.nodeid());
      break;
    case idl::ConfChangeRemoveNode:
      raft_->removeNode(cc.nodeid());
      break;
    case idl::ConfChangeUpdateNode:
      break;
    default:
      RAFT_PANIC("unexpected conf type");
  }

  return idl::ConfState().nodes(raft_->nodes()).learners(raft_->learnerNodes());
}

error_s RawNode::Step(idl::Message m) {
  // ignore unexpected local messages receiving over network
  if (idl::IsLocalMsg(m.type())) {
    return ErrStepLocalMsg;
  }
  if (!raft_->getProgress(m.from()) || !IsResponseMsg(m.type())) {
    return raft_->Step(std::move(m));
  }
  return ErrStepPeerNotFound;
}

ReadyUPtr RawNode::GetReady() {
  ReadyUPtr rd = newReady();
  raft_->msgs_.clear();
  return rd;
}

bool RawNode::HasReady() {
  Raft &r = *raft_;

  if (r.softState() != prevSoftSt_) {
    return true;
  }
  auto hardSt = r.hardState();
  if (!IsEmptyHardState(hardSt) && !isHardStateEqual(hardSt, prevHardSt_)) {
    return true;
  }
  if (r.raftLog_->unstable_.snapshot != nullptr && !IsEmptySnap(*r.raftLog_->unstable_.snapshot)) {
    return true;
  }
  if (!r.msgs_.empty() || !r.raftLog_->unstableEntries().empty() || r.raftLog_->hasNextEnts()) {
    return true;
  }
  return !r.readStates_.empty();
}

void RawNode::Advance(ReadyUPtr rd) {
  commitReady(std::move(rd));
}

Status RawNode::GetStatus() {
  return raft_->getStatus();
}

void RawNode::ReportUnreachable(uint64_t id) {
  raft_->Step(idl::Message().type(idl::MsgUnreachable).from(id).to(id));
}

void RawNode::ReportSnapshot(uint64_t id, SnapshotStatus status) {
  bool rej = status == kSnapshotFailure;
  raft_->Step(idl::Message().type(idl::MsgSnapStatus).from(id).reject(rej));
}

void RawNode::TransferLeader(uint64_t transferee) {
  raft_->Step(idl::Message().type(idl::MsgTransferLeader).from(transferee));
}

void RawNode::ReadIndex(std::string rctx) {
  raft_->Step(idl::Message().type(idl::MsgReadIndex).entries({idl::Entry().data(std::move(rctx))}));
}

ReadyUPtr RawNode::newReady() {
  return raft_->newReady(prevSoftSt_, prevHardSt_);
}

void RawNode::commitReady(ReadyUPtr rd) {
  if (rd->softState != nullptr) {
    prevSoftSt_ = *rd->softState;
  }
  if (!idl::IsEmptyHardState(rd->hardState)) {
    prevHardSt_ = rd->hardState;
  }
  if (prevHardSt_.commit() != 0) {
    // In most cases, prevHardSt and rd.HardState will be the same
    // because when there are new entries to apply we just sent a
    // HardState with an updated Commit value. However, on initial
    // startup the two are different because we don't send a HardState
    // until something changes, but we do send any un-applied but
    // committed entries (and previously-committed entries may be
    // incorporated into the snapshot, even if rd.CommittedEntries is
    // empty). Therefore we mark all committed entries as applied
    // whether they were included in rd.HardState or not.
    raft_->raftLog_->appliedTo(prevHardSt_.commit());
  }
  if (!rd->entries.empty()) {
    auto e = rd->entries[rd->entries.size() - 1];
    raft_->raftLog_->stableTo(e.index(), e.term());
  }
  if (!idl::IsEmptySnap(rd->snapshot)) {
    raft_->raftLog_->stableSnapTo(rd->snapshot.metadata_index());
  }
  if (!rd->readStates.empty()) {
    raft_->readStates_.clear();
  }
}

}  // namespace yaraft
