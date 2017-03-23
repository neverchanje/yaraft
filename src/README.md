This file contains additional notes for yaraft, which supplement
the comments present in the code.

## Internal Raft State Machine

| Role | Message | Action |
|------|---------|--------|
|Any|Any message with term < `currentTerm`|Ignore this message|
|Any|Any message contains term T > `currentTerm`|Set `currentTerm` = T, then process this message as normal.|
|Any|MsgVote|[Handle RequestVote](#handle-requestvote)|
|Leader|MsgAppResp|[Handle MsgAppResp](#handle-msgappresp)|
|Leader|MsgHeartbeatResp|Resend AppendEntries when the follower was detected falling behind.|
|Leader|MsgVoteResp|[Handle MsgVoteResp](#handle-requestvote-response)|
|Leader|MsgProp|Handle MsgProp|
|Follower|MsgHup|[Convert to Candidate](#convert-to-candidate-requestvote)|
|Follower|MsgApp|[Handle AppendEntries](#handle-appendentries)|
|Follower|MsgTimeoutNow||

|Server (Stable)|Description|Initial Value|
|------|-----------|-------------|
|currentTerm|The server's term number.|Loaded from stable storage.|
|state|The server's state (Follower, Candidate, or Leader).|Follower|
|votedFor|The candidate the server voted for in its current term, or Nil if it hasn't voted for any.|Nil|

|Server (Volatile)|Description|Initial Value|
|------|-----------|-------------|
|commitIndex|Index of highest log entry known to be committed. (increases monotonically)|0|

|Server (Others)|Description|Initial Value|
|------|-----------|-------------|
|currentLeader|The leader in current raft group.|Nil|

|Leader|Description|Initial Value|
|------|-----------|-------------|
|Progress|The replication progress the leader holds for each follower|\\|


|Progress|Description|Initial Value|
|------|-----------|-------------|
|nextIndex|The next entry to send to each follower. nextIndex is always larger than prevLogIndex.|\\|
|matchIndex|The latest entry that each follower has acknowledged is the same as the leader's. This is used to calculate commitIndex on the leader.|\\|

### Convert to Follower
- Set `currentLeader` to where the message came from. 
- Reset election timer + Reset random election timeout.
- Set `currentTerm` to the leader's term.
- Reset `votedFor` to nil.

### Convert to Candidate (RequestVote)
- Increment `currentTerm`.
- Vote for itself.
- Reset election timer + Reset random election timeout.
- Set `state` to Candidate.
- Send RequestVote RPCs to all other servers.
- *TODO*: Do not resend RequestVote to those have granted the vote.
- Set `currentLeader` to null.

### Convert to Leader (BecomeLeader)

@see `Raft::becomeLeader`

1. Assert(state == Candidate)
2. Set `currentLeader` to itself.
3. Reinitialize `nextIndex[]`, and `matchIndex[]`. In order to calculating the
largest index of committed entry, leader's `matchIndex` should be maintained
as well.

### Timer
- heartbeat timer
- election timer

### NextIndex Decrease

### Restart

### Handle RequestVote

- Assert(m.Term == `currentTerm`)
- Condition for granting the vote: if the MsgVote is at least as "up-to-date" as the voter's latest log,
and either if we have voted for the same candidate, or we haven't voted for any candidate.
- Set `votedFor` when we grant the vote.

### Handle RequestVote Response

@see `Raft::handleMsgVoteResp`

Once the number of granted MsgVoteResps the candidate has received grows to quorum of
the cluster (`len(peers)/2+1`), it'll convert to leader and broadcast AppendEntries to other
servers. Likewise, when the number of rejected votes grows to quorum, candidate will immediately
step down to follower.

Consider a problem: how does the rejected candidate know who is the leader? Actually it doesn't
know anything about leader.

It's obvious that after the candidate elected, it still receives MsgVoteResp. 
But the new leader just ignore them (leader doesn't handle MsgVoteResp).

### Handle AppendEntries

@see `Raft::handleAppendEntries`

- Assert(state == Follower)
- Reject if log doesn’t contain an entry at `prevLogIndex` whose term matches `prevLogTerm`. 
- If `leaderCommit > commitIndex`, set `commitIndex = min(leaderCommit, index of last new entry)`.
More specifically:
```c
newLastIndex = prevLogIndex + sizeof(entries);
newCommitIndex = min(leaderCommit, newLastIndex);
if(newCommitIndex > commitIndex) {
    // commitIndex never decreases
    if(newCommitIndex > lastIndex)
        error_handling("")
    commitIndex = newCommitIndex;
}
```
Given a corner case: What if `commitIndex > prevLogIndex` ? 

@see `RaftLog::MaybeAppend`

- If an existing entry conflicts with a new one (same index but different terms), 
delete the existing entry and all that follow it.
- Append any new entries not already in the log.
- NOTE: Logs are first saved in `unstable` when being appended, until the library user flushing 
them into stable storage.
- NOTE: If MaybeAppend tries to append an array entries and all of which are existed, it will not
delete the following log entries. The deleted should only follows an conflicted one.

@see `Progress::MaybeUpdate`

- Update the matchIndex and nextIndex of the follower, when a successful MsgAppResp is returned.

### Handle MsgProp

#### Leader 

@see `Raft::handleLeaderMsgProp` 

When MsgProp is passed to leader, each of the log entries in message should be included with
`term = leader's currentTerm` and `index = index of last entry + offset`.
And the entries will be appended to leader's log.

After that leader will update `MatchIndex[]`, `NextIndex[]`, and broadcast AppendEntries to all peers.

### Handle MsgAppResp

@see `Raft::handleMsgAppResp`

- Leader advances `nextIndex` and `matchIndex` if MsgApp was accepted.
- If more than half number of nodes have accepted the MsgApp, leader attempts to advance
commitIndex to the largest index of log having replicated on majority (use `matchIndex`).
- If MsgApp was rejected, leader decreases the `nextIndex` of that peer and retry.

### Single-Node Cluster

If there's only single node in the cluster, whenever it receives a MsgHup, 
it will immediately convert to Leader state.

### Sending AppendEntries RPC

Entries in an AppendEntries RPC are limited with maximum size given by configuration
(`Conf::maxSizePerMsg`),
in order for flow control.

To preserve the AppendEntries consistency, each of the MsgApp message should carry with
two arguments `prevLogIndex`, which = `nextIndex - 1`, and `prevLogTerm`, which is the term
of entry at `prevLogIndex`.

## Messages

### MsgAppResp

|Arguments||
|-------|---|
|Reject|True if the AppendEntries failed.|
|RejectHint|Returns index of the last log entry. Empty if the AppendEntries is accepted.|
|Index|If rejected, returns prevLogIndex, otherwise returns index of the last entry.|