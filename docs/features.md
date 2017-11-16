# Features in yaraft

yaraft doesn't support the [full list of features](https://github.com/coreos/etcd/tree/master/raft#features) 
included in etcd/raft.

- [x] Leader election
- [x] Log replication
- [x] Log Compaction / InstallSnapshot
- [x] Membership changes
- [ ] Leader Transfer
- [x] Linearizable read-only queries
- [x] Pipelining
- [ ] Flow Control
- [x] Batching Raft messages
- [x] Batching log entries
- [ ] Proposal forwarding from followers to leader
- [ ] CheckQuorum
- [x] PreVote

Leader election, log replication, and log compaction are the most basic functions that the Raft protocol provides. 
Read [Raft paper](https://raft.github.io/raft.pdf) or [Raft thesis](https://ramcloud.stanford.edu/~ongaro/thesis.pdf) 
for more knowledge about the algorithm.

Membership changes means dynamically adding or removing nodes from cluster. It's two methods stated in
Raft thesis chapter 4 on this problem. The method 1 restricts that servers can only be added or removed 
one by one, and the method 2 allows arbitrary membership changes but is more complicated.
We only implemented the single-server approach.

Raft thesis 6.4 discussed the two techniques to efficiently handling linearizable read-only queries. In method 1 leader keeps a readIndex for each of the read queries and issues a new round of heartbeat to ensure no newer leader. When the committedIndex advances as far as the readIndex, the read on leader will be sufficiently consistent. The methods 2 relies on real clocks is stated in Raft thesis 6.4.1. It's implemented in etcd/raft but is not included in yaraft.

Batching and pipelining is discussed in Raft thesis 10.2.2. Batching of log entries is naturally supported by Raft, and in yaraft the leader optimistically replicates the log entries to the follower that is in StateReplicate, which means that it's safe for pipelining. If the follower rejects for the AppendEntries, the leader will stop pipelining and wait for the prior entry to be acknowledged.

PreVote is an optimization on the voting process stated in Raft thesis 9.6. It solves the issue of a partitioned server disrupting the cluster when it rejoins.