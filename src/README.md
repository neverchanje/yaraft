## Internal Raft State Machine

| Role | Message | Action |
|------|---------|--------|
|Any|Any message with term < `currentTerm`|Ignore this message|
|Any|Any message contains term T > `currentTerm`|Set `currentTerm` = T, then process this message as normal.|
|Any|MsgVote|[Handle RequestVote](#handle-requestvote)|
|Leader|MsgBeat|Broadcast heartbeat messges to all peers.|
|Leader|Rejected MsgApp (MsgAppResp with rejection)|Decrease `nextIndex` and retry.|
|Leader|Committed MsgApp (OK MsgAppResp)|Update `matchIndex` to MsgAppResp.Index `n` if `matchIndex` < `n`. Update `nextIndex` to `n+1` if `nextIndex` < `n+1`. If matchIndex were updated, |
|Leader|MsgHeartbeatResp|Handle AppendEntries|
|Follower|MsgHup|[Convert to Candidate](#convert-to-candidate-requestvote)|
|Follower|MsgApp|Reset election timer. |
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
|nextIndex|The next entry to send to each follower.|\\|
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

1. Assert(state == Candidate)
2. Set `currentLeader` to itself.

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

### Handle AppendEntries

- Assert(state == Follower)
- Assert(m.Term == `currentTerm`)
- Assert(The follower has an entry with the same index and term as prevLogIndex and prevLogTerm). 
More specifically, `prevLogIndex <= LastIndex() && prevLogTerm = log[prevLogIndex].Term`.

### Single-Node Cluster

If there's only a single node in the cluster, whenever it receives a MsgHup, 
it will immediately convert to Leader state.
