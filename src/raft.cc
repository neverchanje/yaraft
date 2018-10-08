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

#include "raft.h"
#include "inflights.h"

namespace yaraft {

/*static*/ Raft *Raft::New(Config *rawCfg) {
  std::unique_ptr<Config> c(rawCfg);
  error_s err = c->Validate();
  if (!err.is_ok()) {
    RAFT_PANIC(err);
  }
  auto raftLog = make_unique<RaftLog>(c->storage, c->maxSizePerMsg);

  idl::HardState hs{};
  idl::ConfState cs{};
  err = c->storage->InitialState(&hs, &cs);
  if (!err.is_ok()) {
    RAFT_PANIC(err);
  }

  auto peers = c->peers;
  auto learners = c->learners;
  if (!cs.nodes().empty() || !cs.learners().empty()) {
    if (!peers.empty() || !learners.empty()) {
      // TODO(bdarnell): the peers argument is always nil except in
      // tests; the argument should be removed and these tests should be
      // updated to specify their nodes through a snapshot.
      RAFT_PANIC("cannot specify both newRaft(peers, learners) and ConfState.(Nodes, Learners)");
    }
    peers = cs.nodes();
    learners = cs.learners();
  }

  auto r = new Raft;
  r->id_ = c->id;
  r->lead_ = kNone;
  r->isLearner_ = false;
  r->raftLog_ = std::move(raftLog);
  r->maxMsgSize_ = c->maxSizePerMsg;
  r->maxInflight_ = c->maxInflightMsgs;
  r->electionTimeout_ = c->electionTick;
  r->heartbeatTimeout_ = c->heartbeatTick;
  r->checkQuorum_ = c->checkQuorum;
  r->preVote_ = c->preVote;
  r->readOnly_ = newReadOnly(c->readOnlyOption);
  r->disableProposalForwarding_ = c->disableProposalForwarding;

  for (auto p : peers) {
    r->prs_[p] = std::make_shared<Progress>();
    r->prs_[p]->Next(1).Ins(r->maxInflight_);
  }
  for (auto p : learners) {
    if (r->prs_.find(p) != r->prs_.end()) {
      RAFT_LOG(PANIC, "node %x is in both learner and peer list", p);
    }
    r->learnerPrs_[p] = std::make_shared<Progress>();
    r->learnerPrs_[p]->Next(1).Ins(r->maxInflight_).IsLearner(true);
    if (r->id_ == p) {
      r->isLearner_ = true;
    }
  }

  if (!idl::IsEmptyHardState(hs)) {
    r->loadState(hs);
  }
  if (c->applied > 0) {
    r->raftLog_->appliedTo(c->applied);
  }
  r->becomeFollower(r->term_, 0);

  std::string nodeStr;
  if (!peers.empty()) {
    nodeStr = std::to_string(*peers.begin());
    std::for_each(std::next(peers.begin()), peers.end(), [&](uint64_t p) { nodeStr += ", " + std::to_string(p); });
  }

  RAFT_LOG(INFO, "newRaft %x [peers: [%s], term: %d, commit: %d, applied: %d, lastindex: %d, lastterm: %d]",
           r->id_, nodeStr, r->term_, r->raftLog_->committed_, r->raftLog_->applied_, r->raftLog_->lastIndex(), r->raftLog_->lastTerm());
  return r;
}

void Raft::send(idl::Message m) {
  m.from(id_);
  if (m.type() == idl::MsgVote || m.type() == idl::MsgVoteResp || m.type() == idl::MsgPreVote || m.type() == idl::MsgPreVoteResp) {
    if (m.term() == 0) {
      // All {pre-,}campaign messages need to have the term set when
      // sending.
      // - MsgVote: m.Term is the term the node is campaigning for,
      //   non-zero as we increment the term when campaigning.
      // - MsgVoteResp: m.Term is the new r.Term if the MsgVote was
      //   granted, non-zero for the same reason MsgVote is
      // - MsgPreVote: m.Term is the term the node will campaign,
      //   non-zero as we use m.Term to indicate the next term we'll be
      //   campaigning for
      // - MsgPreVoteResp: m.Term is the term received in the original
      //   MsgPreVote if the pre-vote was granted, non-zero for the
      //   same reasons MsgPreVote is
      RAFT_LOG(PANIC, "term should be set when sending %s", m.type());
    }
  } else {
    if (m.term() != 0) {
      RAFT_LOG(PANIC, "term should not be set when sending %s (was %d)", m.type(), m.term());
    }
    // do not attach term to MsgProp, MsgReadIndex
    // proposals are a way to forward to the leader and
    // should be treated as local message.
    // MsgReadIndex is also forwarded to leader.
    if (m.type() != idl::MsgProp && m.type() != idl::MsgReadIndex) {
      m.term(term_);
    }
  }
  msgs_.push_back(std::move(m));
}

bool Raft::maybeSendAppend(uint64_t to, bool sendIfEmpty) {
  auto pr = getProgress(to);
  if (pr->IsPaused()) {
    return false;
  }
  idl::Message m;
  m.to(to);

  auto errt = raftLog_->term(pr->next_ - 1);
  auto erre = raftLog_->entries(pr->next_, maxMsgSize_);
  idl::EntryVec ents;
  if (erre.is_ok()) {
    ents = std::move(erre.get_value());
  }
  if (ents.empty() && !sendIfEmpty) {
    return false;
  }

  if (!errt.is_ok() || !erre.is_ok()) {  // send snapshot if we failed to get term or entries
    if (!pr->recentActive_) {
      RAFT_LOG(DEBUG, "ignore sending snapshot to %x since it is not recently active", to);
      return false;
    }

    m.type(idl::MsgSnap);
    auto errSnap = raftLog_->snapshot();
    if (!errSnap.is_ok()) {
      if (errSnap.get_error() == ErrSnapshotTemporarilyUnavailable) {
        RAFT_LOG(DEBUG, "%x failed to send snapshot to %x because snapshot is temporarily unavailable", id_, to);
        return false;
      }
      RAFT_PANIC(errSnap.get_error());
    }

    idl::Snapshot snapshot = std::move(errSnap.get_value());
    if (IsEmptySnap(snapshot)) {
      RAFT_PANIC("need non-empty snapshot");
    }
    m.snapshot(snapshot);
    uint64_t sindex = snapshot.metadata_index(), sterm = snapshot.metadata_term();
    RAFT_LOG(DEBUG, "%x [firstIndex: %d, commit: %d] sent snapshot[index: %d, term: %d] to %x [%s]",
             id_, raftLog_->firstIndex(), raftLog_->committed_, sindex, sterm, to, pr->ToString());
    pr->becomeSnapshot(sindex);
    RAFT_LOG(DEBUG, "%x paused sending replication messages to %x [%s]", id_, to, pr->ToString());
  } else {
    uint64_t term = errt.get_value();
    m.type(idl::MsgApp)
        .index(pr->next_ - 1)
        .logterm(term)
        .entries(std::move(ents))
        .commit(raftLog_->committed_);
    size_t n = m.entries().size();
    if (n != 0) {
      switch (pr->state_) {
        case Progress::kStateReplicate: {
          // optimistically increase the next when in ProgressStateReplicate
          uint64_t last = m.entries()[n - 1].index();
          pr->optimisticUpdate(last);
          pr->ins_->add(last);
          break;
        }
        case Progress::kStateProbe: {
          pr->pause();
          break;
        }
        default: {
          RAFT_LOG(PANIC, "%x is sending append in unhandled state %s", id_, pr->state_);
          break;
        }
      }
    }
  }
  send(m);
  return true;
}

error_s Raft::stepLeader(idl::Message &m) {
  // These message types do not require any progress for m.From.
  switch (m.type()) {
    case idl::MsgBeat:
      bcastHeartbeat();
      return error_s::ok();
    case idl::MsgCheckQuorum:
      if (!checkQuorumActive()) {
        RAFT_LOG(WARN, "%x stepped down to follower since quorum is not active", id_);
        becomeFollower(term_, kNone);
      }
      return error_s::ok();
    case idl::MsgProp:
      if (m.entries().empty()) {
        RAFT_LOG(PANIC, "%x stepped empty MsgProp", id_);
      }
      if (prs_.find(id_) == prs_.end()) {
        // If we are not currently a member of the range (i.e. this node
        // was removed from the configuration while serving as leader),
        // drop any new proposals.
        return ErrProposalDropped;
      }
      if (leadTransferee_ != kNone) {
        RAFT_LOG(DEBUG, "%x [term %d] transfer leadership to %x is in progress; dropping proposal", id_, term_, leadTransferee_);
        return ErrProposalDropped;
      }

      for (size_t i = 0; i < m.entries().size(); i++) {
        auto e = m.entries()[i];
        if (e.type() == idl::EntryConfChange) {
          if (pendingConfIndex_ > raftLog_->applied_) {
            RAFT_LOG(INFO, "propose conf %s ignored since pending unapplied configuration [index %d, applied %d]",
                     e, pendingConfIndex_, raftLog_->applied_);
            m.entries()[i] = idl::Entry().type(idl::EntryType::EntryNormal);
          } else {
            pendingConfIndex_ = raftLog_->lastIndex() + uint64_t(i) + 1;
          }
        }
      }
      appendEntry(std::move(m.entries()));
      bcastAppend();
      return error_s::ok();
    case idl::MsgReadIndex: {
      if (quorum() > 1) {
        if (raftLog_->zeroTermOnErrCompacted(raftLog_->term(raftLog_->committed_)) != term_) {
          // Reject read only request when this leader has not committed any log entry at its term.
          return error_s::ok();
        }

        // thinking: use an interally defined context instead of the user given context.
        // We can express this in terms of the term and index instead of a user-supplied value.
        // This would allow multiple reads to piggyback on the same message.
        switch (readOnly_->option) {
          case kReadOnlySafe:
            readOnly_->addRequest(raftLog_->committed_, m);
            bcastHeartbeatWithCtx(m.entries()[0].data());
            break;
          case kReadOnlyLeaseBased:
            uint64_t ri = raftLog_->committed_;
            if (m.from() == kNone || m.from() == id_) {  // from local member
              readStates_.push_back(std::move(ReadState().Index(raftLog_->committed_).RequestCtx(m.entries()[0].data())));
            } else {
              send(idl::Message().to(m.from()).type(idl::MsgReadIndexResp).index(ri).entries(std::move(m.entries())));
            }
            break;
        }
      } else {
        readStates_.push_back(std::move(ReadState().Index(raftLog_->committed_).RequestCtx(m.entries()[0].data())));
      }
      return error_s::ok();
    }
    default:
      break;
  }

  // All other message types require a progress for m.From (pr).
  auto pr = getProgress(m.from());
  if (pr == nullptr) {
    RAFT_LOG(DEBUG, "%x no progress available for %x", id_, m.from());
    return error_s::ok();
  }

  switch (m.type()) {
    case idl::MsgAppResp: {
      pr->recentActive_ = true;

      if (m.reject()) {
        RAFT_LOG(DEBUG, "%x received msgApp rejection(lastindex: %d) from %x for index %d",
                 id_, m.rejecthint(), m.from(), m.index());
        if (pr->maybeDecrTo(m.index(), m.rejecthint())) {
          RAFT_LOG(DEBUG, "%x decreased progress of %x to [%s]", id_, m.from(), pr->ToString());
          if (pr->state_ == Progress::kStateReplicate) {
            pr->becomeProbe();
          }
          sendAppend(m.from());
        }
      } else {
        bool oldPaused = pr->IsPaused();
        if (pr->maybeUpdate(m.index())) {
          if (pr->state_ == Progress::kStateProbe) {
            pr->becomeReplicate();
          } else if (pr->state_ == Progress::kStateSnapshot && pr->needSnapshotAbort()) {
            RAFT_LOG(DEBUG, "%x snapshot aborted, resumed sending replication messages to %x [%s]", id_, m.from(), pr);
            pr->becomeProbe();
          } else if (pr->state_ == Progress::kStateReplicate) {
            pr->ins_->freeTo(m.index());
          }

          if (maybeCommit()) {
            bcastAppend();
          } else if (oldPaused) {
            // If we were paused before, this node may be missing the
            // latest commit index, so send it.
            sendAppend(m.from());
          }
          // We've updated flow control information above, which may
          // allow us to send multiple (size-limited) in-flight messages
          // at once (such as when transitioning from probe to
          // replicate, or when freeTo() covers multiple messages). If
          // we have more entries to send, send as many messages as we
          // can (without sending empty messages for the commit index)
          while (maybeSendAppend(m.from(), false)) {
          }
          // Transfer leadership is in progress.
          if (m.from() == leadTransferee_ && pr->match_ == raftLog_->lastIndex()) {
            RAFT_LOG(INFO, "%x sent MsgTimeoutNow to %x after received MsgAppResp", id_, m.from());
            sendTimeoutNow(m.from());
          }
        }
      }
      break;
    }
    case idl::MsgHeartbeatResp: {
      pr->recentActive_ = true;
      pr->resume();

      // free one slot for the full inflights window to allow progress.
      if (pr->state_ == Progress::kStateReplicate && pr->ins_->full()) {
        pr->ins_->freeFirstOne();
      }
      if (pr->match_ < raftLog_->lastIndex()) {
        sendAppend(m.from());
      }

      if (readOnly_->option != kReadOnlySafe || m.context().empty()) {
        return error_s::ok();
      }

      size_t ackCount = readOnly_->recvAck(m);
      if (ackCount < quorum()) {
        return error_s::ok();
      }

      auto rss = readOnly_->advance(m);
      for (auto rs : rss) {
        auto &req = rs.req;
        if (req.from() == kNone || req.from() == id_) {  // from local member
          readStates_.push_back(ReadState().Index(rs.index).RequestCtx(req.entries()[0].data()));
        } else {
          send(idl::Message().to(req.from()).type(idl::MsgReadIndexResp).index(rs.index).entries(std::move(req.entries())));
        }
      }
      break;
    }
    case idl::MsgSnapStatus:
      if (pr->state_ != Progress::kStateSnapshot) {
        return error_s::ok();
      }
      if (!m.reject()) {
        pr->becomeProbe();
        RAFT_LOG(DEBUG, "%x snapshot succeeded, resumed sending replication messages to %x [%s]", id_, m.from(), pr);
      } else {
        pr->snapshotFailure();
        pr->becomeProbe();
        RAFT_LOG(DEBUG, "%x snapshot failed, resumed sending replication messages to %x [%s]", id_, m.from(), pr);
      }
      // If snapshot finish, wait for the msgAppResp from the remote node before sending
      // out the next msgApp.
      // If snapshot failure, wait for a heartbeat interval before next try
      pr->pause();
      break;
    case idl::MsgUnreachable:
      // During optimistic replication, if the remote becomes unreachable,
      // there is huge probability that a MsgApp is lost.
      if (pr->state_ == Progress::kStateReplicate) {
        pr->becomeProbe();
      }
      RAFT_LOG(DEBUG, "%x failed to send message to %x because it is unreachable [%s]", id_, m.from(), pr);
      break;
    case idl::MsgTransferLeader: {
      if (pr->isLearner_) {
        RAFT_LOG(DEBUG, "%x is learner. Ignored transferring leadership", id_);
        return error_s::ok();
      }
      uint64_t leadTransferee = m.from();
      uint64_t lastLeadTransferee = leadTransferee_;
      if (lastLeadTransferee != kNone) {
        if (lastLeadTransferee == leadTransferee) {
          RAFT_LOG(INFO,
                   "%x [term %d] transfer leadership to %x is in progress, ignores request to same node %x",
                   id_, term_, leadTransferee, leadTransferee);
          return error_s::ok();
        }
        abortLeaderTransfer();
        RAFT_LOG(INFO, "%x [term %d] abort previous transferring leadership to %x", id_, term_, lastLeadTransferee);
      }
      if (leadTransferee == id_) {
        RAFT_LOG(DEBUG, "%x is already leader. Ignored transferring leadership to self", id_);
        return error_s::ok();
      }
      // Transfer leadership to third party.
      RAFT_LOG(INFO, "%x [term %d] starts to transfer leadership to %x", id_, term_, leadTransferee);
      // Transfer leadership should be finished in one electionTimeout, so reset r.electionElapsed.
      electionElapsed_ = 0;
      leadTransferee_ = leadTransferee;
      if (pr->match_ == raftLog_->lastIndex()) {
        sendTimeoutNow(leadTransferee);
        RAFT_LOG(INFO, "%x sends MsgTimeoutNow to %x immediately as %x already has up-to-date log", id_, leadTransferee, leadTransferee);
      } else {
        sendAppend(leadTransferee);
      }
    }
    default:
      break;
  }
  return error_s::ok();
}

error_s Raft::Step(idl::Message m) {
  // Handle the message term, which may result in our stepping down to a follower.
  if (m.term() == 0) {
    // local message
  } else if (m.term() > term_) {
    if (m.type() == idl::MsgVote || m.type() == idl::MsgPreVote) {
      bool force = m.context() == campaignTransfer;
      bool inLease = checkQuorum_ && lead_ != 0 && electionElapsed_ < electionTimeout_;
      if (!force && inLease) {
        // If a server receives a RequestVote request within the minimum election timeout
        // of hearing from a current leader, it does not update its term or grant its vote
        RAFT_LOG(INFO, "%x [logterm: %d, index: %d, vote: %x] ignored %s from %x [logterm: %d, index: %d] at term %d: lease is not expired (remaining ticks: %d)",
                 id_, raftLog_->lastTerm(), raftLog_->lastIndex(), vote_, m.type(), m.from(), m.logterm(), m.index(), term_, electionTimeout_ - electionElapsed_);
        return error_s::ok();
      }
    }

    if (m.type() == idl::MsgPreVote) {
      // Never change our term in response to a PreVote
    } else if (m.type() == idl::MsgPreVoteResp && !m.reject()) {
      // We send pre-vote requests with a term in our future. If the
      // pre-vote is granted, we will increment our term when we get a
      // quorum. If it is not, the term comes from the node that
      // rejected our vote so we should become a follower at the new
      // term.
    } else {
      RAFT_LOG(INFO, "%x [term: %d] received a %s message with higher term from %x [term: %d]",
               id_, term_, m.type(), m.from(), m.term());
      if (m.type() == idl::MsgApp || m.type() == idl::MsgHeartbeat || m.type() == idl::MsgSnap) {
        becomeFollower(m.term(), m.from());
      } else {
        becomeFollower(m.term(), 0);
      }
    }
  } else if (term_ > m.term()) {
    if ((checkQuorum_ || preVote_) && (m.type() == idl::MsgHeartbeat || m.type() == idl::MsgApp)) {
      // We have received messages from a leader at a lower term. It is possible
      // that these messages were simply delayed in the network, but this could
      // also mean that this node has advanced its term number during a network
      // partition, and it is now unable to either win an election or to rejoin
      // the majority on the old term. If checkQuorum is false, this will be
      // handled by incrementing term numbers in response to MsgVote with a
      // higher term, but if checkQuorum is true we may not advance the term on
      // MsgVote and must generate other messages to advance the term. The net
      // result of these two features is to minimize the disruption caused by
      // nodes that have been removed from the cluster's configuration: a
      // removed node will send MsgVotes (or MsgPreVotes) which will be ignored,
      // but it will not receive MsgApp or MsgHeartbeat, so it will not create
      // disruptive term increases, by notifying leader of this node's activeness.
      // The above comments also true for Pre-Vote
      //
      // When follower gets isolated, it soon starts an election ending
      // up with a higher term than leader, although it won't receive enough
      // votes to win the election. When it regains connectivity, this response
      // with "pb.MsgAppResp" of higher term would force leader to step down.
      // However, this disruption is inevitable to free this stuck node with
      // fresh election. This can be prevented with Pre-Vote phase.
      send(idl::Message().to(m.from()).type(idl::MsgAppResp));
    } else if (m.type() == idl::MsgPreVote) {
      // Before Pre-Vote enable, there may have candidate with higher term,
      // but less log. After update to Pre-Vote, the cluster may deadlock if
      // we drop messages with a lower term.
      RAFT_LOG(INFO, "%x [logterm: %d, index: %d, vote: %x] rejected %s from %x [logterm: %d, index: %d] at term %d",
               id_, raftLog_->lastTerm(), raftLog_->lastIndex(), vote_, m.type(), m.from(), m.logterm(), m.index(), term_);
      send(idl::Message().to(m.from()).term(term_).type(idl::MsgPreVoteResp).reject(true));
    } else {
      // ignore other cases
      RAFT_LOG(INFO, "%x [term: %d] ignored a %s message with lower term from %x [term: %d]",
               id_, term_, m.type(), m.from(), m.term());
    }
    return error_s::ok();
  }

  switch (m.type()) {
    case idl::MsgHup: {
      if (state_ != StateType::kLeader) {
        auto errEnts = raftLog_->slice(raftLog_->applied_ + 1, raftLog_->committed_ + 1, noLimit);
        if (!errEnts.is_ok()) {
          RAFT_LOG(PANIC, "unexpected error getting unapplied entries (%v)", errEnts.get_error());
        }
        size_t n = numOfPendingConf(errEnts.get_value());
        if (n != 0 && raftLog_->committed_ > raftLog_->applied_) {
          RAFT_LOG(WARN, "%x cannot campaign at term %d since there are still %d pending configuration changes to apply", id_, term_, n);
          return error_s::ok();
        }

        RAFT_LOG(INFO, "%x is starting a new election at term %d", id_, term_);
        if (preVote_) {
          campaign(campaignPreElection);
        } else {
          campaign(campaignElection);
        }
      } else {
        RAFT_LOG(DEBUG, "%x ignoring MsgHup because already leader", id_);
      }

      break;
    }
    case idl::MsgVote:
    case idl::MsgPreVote: {
      if (isLearner_) {
        // TODO: learner may need to vote, in case of node down when confchange.
        RAFT_LOG(INFO, "%x [logterm: %d, index: %d, vote: %x] ignored %s from %x [logterm: %d, index: %d] at term %d: learner can not vote",
                 id_, raftLog_->lastTerm(), raftLog_->lastIndex(), vote_, m.type(), m.from(), m.logterm(), m.index(), term_);
        return error_s::ok();
      }
      // We can vote if this is a repeat of a vote we've already cast...
      bool canVote = vote_ == m.from() ||
                     // ...we haven't voted and we don't think there's a leader yet in this term...
                     (vote_ == kNone && lead_ == kNone) ||
                     // ...or this is a PreVote for a future term...
                     (m.type() == idl::MsgPreVote && m.term() > term_);
      // ...and we believe the candidate is up to date.
      if (canVote && raftLog_->isUpToDate(m.index(), m.logterm())) {
        RAFT_LOG(INFO, "%x [logterm: %d, index: %d, vote: %x] cast %s for %x [logterm: %d, index: %d] at term %d",
                 id_, raftLog_->lastTerm(), raftLog_->lastIndex(), vote_, m.type(), m.from(), m.logterm(), m.index(), term_);
        // When responding to Msg{Pre,}Vote messages we include the term
        // from the message, not the local term. To see why consider the
        // case where a single node was previously partitioned away and
        // it's local term is now of date. If we include the local term
        // (recall that for pre-votes we don't update the local term), the
        // (pre-)campaigning node on the other end will proceed to ignore
        // the message (it ignores all out of date messages).
        // The term in the original message and current local term are the
        // same in the case of regular votes, but different for pre-votes.
        send(idl::Message().to(m.from()).term(m.term()).type(voteRespType(m.type())));
        if (m.type() == idl::MsgVote) {
          // Only record real votes.
          electionElapsed_ = 0;
          vote_ = m.from();
        }
      } else {
        RAFT_LOG(INFO, "%x [logterm: %d, index: %d, vote: %x] rejected %s from %x [logterm: %d, index: %d] at term %d",
                 id_, raftLog_->lastTerm(), raftLog_->lastIndex(), vote_, m.type(), m.from(), m.logterm(), m.index(), term_);
        send(idl::Message().to(m.from()).term(term_).type(voteRespMsgType(m.type())).reject(true));
      }
      break;
    }
    default: {
      error_s err = step(m);
      if (!err.is_ok()) {
        return err;
      }
      break;
    }
  }
  return error_s::ok();
}

void Raft::tickHeartbeat() {
  heartbeatElapsed_++;
  electionElapsed_++;

  if (electionElapsed_ >= electionTimeout_) {
    electionElapsed_ = 0;
    if (checkQuorum_) {
      Step(idl::Message().from(id_).type(idl::MsgCheckQuorum));
    }
    // If current leader cannot transfer leadership in electionTimeout, it becomes leader again
    if (state_ == StateType::kLeader && leadTransferee_ != 0) {
      abortLeaderTransfer();
    }
  }

  if (state_ != StateType::kLeader) {
    return;
  }

  if (heartbeatElapsed_ >= heartbeatTimeout_) {
    heartbeatElapsed_ = 0;
    Step(idl::Message().from(id_).type(idl::MsgBeat));
  }
}

size_t Raft::poll(uint64_t id, idl::MessageType t, bool v) {
  size_t granted = 0;
  if (v) {
    RAFT_LOG(INFO, "%x received %s from %x at term %d", id_, t, id, term_);
  } else {
    RAFT_LOG(INFO, "%x received %s rejection from %x at term %d", id_, t, id, term_);
  }
  if (votes_.find(id) == votes_.end()) {
    votes_[id] = v;
  }
  for (auto p : votes_) {
    bool vv = p.second;
    if (vv) {
      granted++;
    }
  }
  return granted;
}

void Raft::campaign(CampaignType t) {
  uint64_t term = 0;
  idl::MessageType voteMsg;
  if (t == campaignPreElection) {
    becomePreCandidate();
    voteMsg = idl::MsgPreVote;
    // PreVote RPCs are sent for the next term before we've incremented r.Term.
    term = term_ + 1;
  } else {
    becomeCandidate();
    voteMsg = idl::MsgVote;
    term = term_;
  }

  if (quorum() == poll(id_, voteRespType(voteMsg), true)) {
    // We won the election after voting for ourselves (which must mean that
    // this is a single-node cluster). Advance to the next state.
    if (t == campaignPreElection) {
      campaign(campaignElection);
    } else {
      becomeLeader();
    }
    return;
  }

  for (auto p : prs_) {
    uint64_t id = p.first;
    if (id == id_) {
      continue;
    }
    RAFT_LOG(INFO, "%x [logterm: %d, index: %d] sent %s request to %x at term %d",
             id_, raftLog_->lastTerm(), raftLog_->lastIndex(), voteMsg, id, term_);
    std::string ctx;
    if (t == campaignTransfer) {
      ctx = t;
    }
    send(idl::Message().term(term).to(id).type(voteMsg).index(raftLog_->lastIndex()).logterm(raftLog_->lastTerm()).context(std::move(ctx)));
  }
}

void Raft::becomeLeader() {
  if (state_ == StateType::kFollower) {
    RAFT_PANIC("invalid transition [follower -> leader]");
  }

  reset(term_);
  lead_ = id_;
  state_ = StateType::kLeader;

  // Conservatively set the pendingConfIndex to the last index in the
  // log. There may or may not be a pending config change, but it's
  // safe to delay any future proposals until we commit all our
  // pending log entries, and scanning the entire tail of the log
  // could be expensive.
  pendingConfIndex_ = raftLog_->lastIndex();

  idl::EntryVec ents{idl::Entry()};
  appendEntry(ents);
  RAFT_LOG(INFO, "%x became leader at term %d", id_, term_);
}

void Raft::becomeFollower(uint64_t term, uint64_t lead) {
  reset(term);
  lead_ = lead;
  state_ = StateType::kFollower;
  RAFT_LOG(INFO, "%x became follower at term %d", id_, term_);
}

void Raft::becomeCandidate() {
  if (state_ == StateType::kLeader) {
    RAFT_PANIC("invalid transition [leader -> candidate]");
  }
  reset(term_ + 1);
  vote_ = id_;
  state_ = StateType::kCandidate;
  RAFT_LOG(INFO, "%x became candidate at term %d", id_, term_);
}

void Raft::becomePreCandidate() {
  if (state_ == StateType::kLeader) {
    RAFT_PANIC("invalid transition [leader -> pre-candidate]");
  }
  // Becoming a pre-candidate changes our step functions and state,
  // but doesn't change anything else. In particular it does not increase
  // r.Term or change r.Vote.
  votes_.clear();
  state_ = StateType::kPreCandidate;
  RAFT_LOG(INFO, "%x became pre-candidate at term %d", id_, term_);
}

void Raft::sendHeartbeat(uint64_t to, std::string ctx) {
  // Attach the commit as min(to.matched, r.committed).
  // When the leader sends out heartbeat message,
  // the receiver(follower) might not be matched with the leader
  // or it might not have all the committed entries.
  // The leader MUST NOT forward the follower's commit to
  // an unmatched index.
  uint64_t commit = (std::min(getProgress(to)->match_, raftLog_->committed_));
  auto m = idl::Message()
               .to(to)
               .type(idl::MsgHeartbeat)
               .commit(commit)
               .context(std::move(ctx));

  send(std::move(m));
}

void Raft::forEachProgress(std::function<void(uint64_t, std::shared_ptr<Progress> &)> f) {
  for (auto &kv : prs_) {
    f(kv.first, kv.second);
  }

  for (auto &kv : learnerPrs_) {
    f(kv.first, kv.second);
  }
}

bool Raft::checkQuorumActive() {
  int act = 0;

  forEachProgress([&act, this](uint64_t id, std::shared_ptr<Progress> &pr) {
    if (id == id_) {  // self is always active
      act++;
      return;
    }
    if (pr->recentActive_ && !pr->isLearner_) {
      act++;
    }
    pr->recentActive_ = false;
  });

  return act >= quorum();
}

void Raft::setProgress(uint64_t id, uint64_t match, uint64_t next, bool isLearner) {
  if (!isLearner) {
    learnerPrs_.erase(id);

    prs_[id] = std::make_shared<Progress>();
    prs_[id]->Next(next).Match(match).Ins(maxInflight_);
    return;
  }
  if (prs_.find(id) != prs_.end()) {
    RAFT_LOG(PANIC, "%x unexpected changing from voter to learner for %x", id_, id);
  }

  learnerPrs_[id] = std::make_shared<Progress>();
  learnerPrs_[id]->Next(next).Match(match).Ins(maxInflight_).IsLearner(true);
}

void Raft::addNodeOrLearnerNode(uint64_t id, bool isLearner) {
  auto pr = getProgress(id);
  if (pr == nullptr) {
    setProgress(id, 0, raftLog_->lastIndex() + 1, isLearner);
  } else {
    if (isLearner && !pr->isLearner_) {
      // can only change Learner to Voter
      RAFT_LOG(INFO, "%x ignored addLearner: do not support changing %x from raft peer to learner.", id_, id);
      return;
    }

    if (isLearner == pr->isLearner_) {
      // Ignore any redundant addNode calls (which can happen because the
      // initial bootstrapping entries are applied twice).
      return;
    }

    // change Learner to Voter, use origin Learner progress
    learnerPrs_.erase(id);
    pr->isLearner_ = false;
    prs_[id] = std::move(pr);
  }

  if (id_ == id) {
    isLearner_ = isLearner;
  }

  // When a node is first added, we should mark it as recently active.
  // Otherwise, CheckQuorum may cause us to step down if it is invoked
  // before the added node has a chance to communicate with us.
  pr = getProgress(id);
  pr->recentActive_ = true;
}

void Raft::removeNode(uint64_t id) {
  delProgress(id);

  // do not try to commit or abort transferring if there is no nodes in the cluster.
  if (prs_.empty() && learnerPrs_.empty()) {
    return;
  }

  // The quorum size is now smaller, so see if any pending entries can
  // be committed.
  if (maybeCommit()) {
    bcastAppend();
  }
  // If the removed node is the leadTransferee, then abort the leadership transferring.
  if (state_ == StateType::kLeader && leadTransferee_ == id) {
    abortLeaderTransfer();
  }
}

bool Raft::restore(idl::Snapshot s) {
  if (s.metadata_index() <= raftLog_->committed_) {
    return false;
  }
  if (raftLog_->matchTerm(s.metadata_index(), s.metadata_term())) {
    RAFT_LOG(INFO, "%x [commit: %d, lastindex: %d, lastterm: %d] fast-forwarded commit to snapshot [index: %d, term: %d]",
             id_, raftLog_->committed_, raftLog_->lastIndex(), raftLog_->lastTerm(), s.metadata_index(), s.metadata_term());
    raftLog_->commitTo(s.metadata_index());
    return false;
  }

  // The normal peer can't become learner.
  if (!isLearner_) {
    auto learners = s.metadata_conf_state().learners();
    for (uint64_t id : learners) {
      if (id == id_) {
        RAFT_LOG(ERROR, "%x can't become learner when restores snapshot [index: %d, term: %d]", id_, s.metadata_index(), s.metadata_term());
        return false;
      }
    }
  }

  RAFT_LOG(INFO, "%x [commit: %d, lastindex: %d, lastterm: %d] starts to restore snapshot [index: %d, term: %d]",
           id_, raftLog_->committed_, raftLog_->lastIndex(), raftLog_->lastTerm(), s.metadata_index(), s.metadata_term());

  raftLog_->restore(s);
  prs_.clear();
  learnerPrs_.clear();
  restoreNode(s.metadata_conf_state().nodes(), false);
  restoreNode(s.metadata_conf_state().learners(), true);
  return true;
}

void Raft::restoreNode(std::vector<uint64_t> nodes, bool isLearner) {
  for (uint64_t n : nodes) {
    uint64_t match = 0, next = raftLog_->lastIndex() + 1;
    if (n == id_) {
      match = next - 1;
      isLearner_ = isLearner;
    }
    setProgress(n, match, next, isLearner);
    RAFT_LOG(INFO, "%x restored progress of %x [%s]", id_, n, getProgress(n)->ToString());
  }
}

void Raft::handleSnapshot(idl::Message &m) {
  uint64_t sindex = m.snapshot().metadata_index(), sterm = m.snapshot().metadata_term();
  if (restore(m.snapshot())) {
    RAFT_LOG(INFO, "%x [commit: %d] restored snapshot [index: %d, term: %d]",
             id_, raftLog_->committed_, sindex, sterm);
    send(idl::Message().to(m.from()).type(idl::MsgAppResp).index(raftLog_->lastIndex()));
  } else {
    RAFT_LOG(INFO, "%x [commit: %d] ignored snapshot [index: %d, term: %d]",
             id_, raftLog_->committed_, sindex, sterm);
    send(idl::Message().to(m.from()).type(idl::MsgAppResp).index(raftLog_->committed_));
  }
}

void Raft::reset(uint64_t term) {
  if (term_ != term) {
    term_ = term;
    vote_ = kNone;
  }
  lead_ = kNone;

  electionElapsed_ = 0;
  heartbeatElapsed_ = 0;
  resetRandomizedElectionTimeout();

  abortLeaderTransfer();

  votes_.clear();
  forEachProgress([this](uint64_t id, std::shared_ptr<Progress> &pr) {
    bool isLearner = pr->isLearner_;
    pr.reset(new Progress());
    pr->Next(raftLog_->lastIndex() + 1).Ins(maxInflight_).IsLearner(isLearner);
    if (id == id_) {
      pr->Match(raftLog_->lastIndex());
    }
  });

  pendingConfIndex_ = 0;
  readOnly_ = newReadOnly(readOnly_->option);
}

void Raft::appendEntry(idl::EntryVec es) {
  uint64_t li = raftLog_->lastIndex();
  for (int i = 0; i < es.size(); i++) {
    es[i].term(term_);
    es[i].index(li + 1 + uint64_t(i));
  }
  // use latest "last" index after truncate/append
  li = raftLog_->append(es);
  getProgress(id_)->maybeUpdate(li);
  // Regardless of maybeCommit's return, our caller will call bcastAppend.
  maybeCommit();
}

error_s Raft::stepCandidate(idl::Message &m) {
  // Only handle vote responses corresponding to our candidacy (while in
  // StateCandidate, we may get stale MsgPreVoteResp messages in this term from
  // our pre-candidate state).
  idl::MessageType myVoteRespType;
  if (state_ == StateType::kPreCandidate) {
    myVoteRespType = idl::MsgPreVoteResp;
  } else {
    myVoteRespType = idl::MsgVoteResp;
  }
  switch (m.type()) {
    case idl::MsgProp:
      RAFT_LOG(INFO, "%x no leader at term %d; dropping proposal", id_, term_);
      return ErrProposalDropped;
    case idl::MsgApp:
      becomeFollower(m.term(), m.from());  // always m.Term == r.Term
      handleAppendEntries(m);
      break;
    case idl::MsgHeartbeat:
      becomeFollower(m.term(), m.from());  // always m.Term == r.Term
      handleHeartbeat(m);
      break;
    case idl::MsgSnap:
      becomeFollower(m.term(), m.from());  // always m.Term == r.Term
      handleSnapshot(m);
      break;
    case idl::MsgTimeoutNow:
      RAFT_LOG(DEBUG, "%x [term %d state %v] ignored MsgTimeoutNow from %x", id_, term_, state_, m.from());
      break;
    default:
      if (m.type() == myVoteRespType) {
        size_t gr = poll(m.from(), m.type(), !m.reject());
        RAFT_LOG(INFO, "%x [quorum:%d] has received %d %s votes and %d vote rejections", id_, quorum(), gr, m.type(), votes_.size() - gr);

        size_t qr = quorum();
        if (qr == gr) {
          if (state_ == StateType::kPreCandidate) {
            campaign(campaignElection);
          } else {
            becomeLeader();
            bcastAppend();
          }
        } else if (qr == votes_.size() - gr) {
          // pb.MsgPreVoteResp contains future term of pre-candidate
          // m.Term > r.Term; reuse r.Term
          becomeFollower(term_, kNone);
        }
      }
      break;
  }
  return error_s::ok();
}

error_s Raft::stepFollower(idl::Message &m) {
  switch (m.type()) {
    case idl::MsgProp:
      if (lead_ == kNone) {
        RAFT_LOG(INFO, "%x no leader at term %d; dropping proposal", id_, term_);
        return ErrProposalDropped;
      } else if (disableProposalForwarding_) {
        RAFT_LOG(INFO, "%x not forwarding to leader %x at term %d; dropping proposal", id_, lead_, term_);
        return ErrProposalDropped;
      }
      m.to(lead_);
      send(m);
      break;
    case idl::MsgApp:
      electionElapsed_ = 0;
      lead_ = m.from();
      handleAppendEntries(m);
      break;
    case idl::MsgHeartbeat:
      electionElapsed_ = 0;
      lead_ = m.from();
      handleHeartbeat(m);
      break;
    case idl::MsgSnap:
      electionElapsed_ = 0;
      lead_ = m.from();
      handleSnapshot(m);
      break;
    case idl::MsgTransferLeader:
      if (lead_ == kNone) {
        RAFT_LOG(INFO, "%x no leader at term %d; dropping leader transfer msg", id_, term_);
        return error_s::ok();
      }
      m.to(lead_);
      send(std::move(m));
      break;
    case idl::MsgTimeoutNow:
      if (promotable()) {
        RAFT_LOG(INFO, "%x [term %d] received MsgTimeoutNow from %x and starts an election to get leadership.", id_, term_, m.from());
        // Leadership transfers never use pre-vote even if r.preVote is true; we
        // know we are not recovering from a partition so there is no need for the
        // extra round trip.
        campaign(campaignTransfer);
      } else {
        RAFT_LOG(INFO, "%x received MsgTimeoutNow from %x but is not promotable", id_, m.from());
      }
      break;
    case idl::MsgReadIndex:
      if (lead_ == kNone) {
        RAFT_LOG(INFO, "%x no leader at term %d; dropping index reading msg", id_, term_);
        return error_s::ok();
      }
      m.to(lead_);
      send(m);
      break;
    case idl::MsgReadIndexResp:
      if (m.entries().size() != 1) {
        RAFT_LOG(ERROR, "%x invalid format of MsgReadIndexResp from %x, entries count: %d", id_, m.from(), m.entries().size());
        return error_s::ok();
      }
      readStates_.push_back(ReadState().Index(m.index()).RequestCtx(m.entries()[0].data()));
      break;
    default:
      break;
  }
  return error_s::ok();
}

void Raft::handleAppendEntries(idl::Message &m) {
  if (m.index() < raftLog_->committed_) {
    send(idl::Message().to(m.from()).type(idl::MsgAppResp).index(raftLog_->committed_));
    return;
  }

  auto p = raftLog_->maybeAppend(m.index(), m.logterm(), m.commit(), m.entries());
  uint64_t mlastIndex = p.first;
  bool ok = p.second;

  if (ok) {
    send(idl::Message().to(m.from()).type(idl::MsgAppResp).index(mlastIndex));
  } else {
    RAFT_LOG(DEBUG, "%x [logterm: %d, index: %d] rejected msgApp [logterm: %d, index: %d] from %x",
             id_, raftLog_->zeroTermOnErrCompacted(raftLog_->term(m.index())), m.index(), m.logterm(), m.index(), m.from());
    send(idl::Message().to(m.from()).type(idl::MsgAppResp).index(m.index()).reject(true).rejecthint(raftLog_->lastIndex()));
  }
}

void Raft::bcastHeartbeat() {
  std::string lastCtx = readOnly_->lastPendingRequestCtx();
  if (lastCtx.empty()) {
    bcastHeartbeatWithCtx("");
  } else {
    bcastHeartbeatWithCtx(std::move(lastCtx));
  }
}

bool Raft::maybeCommit() {
  // TODO(bmizerany): optimize.. Currently naive
  std::vector<uint64_t> mis;
  for (auto kv : prs_) {
    auto p = kv.second;
    mis.push_back(p->match_);
  }
  std::sort(mis.begin(), mis.end(), std::greater<uint64_t>());
  uint64_t mci = mis[quorum() - 1];
  return raftLog_->maybeCommit(mci, term_);
}

Status Raft::getStatus() const {
  Status s;
  s.id = id_;
  s.leadTransferee = leadTransferee_;

  s.hardState = hardState();
  s.softState = softState();

  s.applied = raftLog_->applied_;

  if (s.softState.raftState == StateType::kLeader) {
    for (auto kv : prs_) {
      s.progress[kv.first] = kv.second->Clone();
    }
    for (auto kv : learnerPrs_) {
      s.progress[kv.first] = kv.second->Clone();
    }
  }
  return s;
}

ReadyUPtr Raft::newReady(const SoftState &prevSoftSt, const idl::HardState &prevHardSt) {
  auto rd = make_unique<Ready>();
  rd->entries = raftLog_->unstableEntries();
  rd->committedEntries = raftLog_->nextEnts();
  rd->messages = msgs_;

  auto softSt = softState();
  if (softSt != prevSoftSt) {
    rd->softState.reset(new SoftState(softSt));
  }

  auto hardSt = hardState();
  if (!isHardStateEqual(hardSt, prevHardSt)) {
    rd->hardState = hardSt;
    // If we hit a size limit when loadaing CommittedEntries, clamp
    // our HardState.Commit to what we're actually returning. This is
    // also used as our cursor to resume for the next Ready batch.
    if (!rd->committedEntries.empty()) {
      auto lastCommit = rd->committedEntries[rd->committedEntries.size() - 1];
      if (rd->hardState.commit() > lastCommit.index()) {
        rd->hardState.commit(lastCommit.index());
      }
    }
  }
  if (raftLog_->unstable_.snapshot != nullptr) {
    rd->snapshot = *raftLog_->unstable_.snapshot;
  }
  if (!readStates_.empty()) {
    rd->readStates = readStates_;
  }
  rd->mustSync = MustSync(rd->hardState, prevHardSt, rd->entries.size());
  return std::move(rd);
}

}  // namespace yaraft
