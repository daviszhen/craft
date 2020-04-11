- [craft](#craft)
  - [What is the Raft?](#what-is-the-raft)
  - [Features of craft](#features-of-craft)
  - [Building and Running](#building-and-running)
    - [OS](#os)
    - [Tools](#tools)
    - [Install gflags](#install-gflags)
    - [How to build](#how-to-build)
    - [Run simulator](#run-simulator)
- [Basic docs](#basic-docs)
  - [Raft Consensus Algorithms](#raft-consensus-algorithms)
  - [Simulator](#simulator)
  - [Formal verification](#formal-verification)
  - [Third party library](#third-party-library)
    - [string operations](#string-operations)
    - [log entries persistence](#log-entries-persistence)
    - [options operations](#options-operations)
    - [log](#log)

# craft
c implementation of raft consensus algorithm

(Raft一致性算法库，用c语言实现。用于构建系统的一致性模块。)

## What is the Raft?
The Raft is the consensus algorithm in a distributed system promoted by Diego Ongaro and John Ousterhout. Two papers [In Search of an Understandable Consensus Algorithm (Extended Version)](https://raft.github.io/raft.pdf) and [Consensus: Bridging Theory and Practice](https://github.com/ongardie/dissertation#readme) give details of the Raft algorithm.
You can get more details about it in https://raft.github.io/

## Features of craft

- leader election
- log replication
- membership changes (single server)
- client interaction
- simulator for test and demonstration
- independent component library (without RPC, Storage inside) that can be embedded into your application.
- concise code

## Building and Running
### OS

    Ubuntu 18.04 Desktop

### Tools
- GCC
- g++
- CMake
- make

### Install gflags
On Debian/Ubuntu Linux, gflags can be installed using the following command:

    sudo apt-get install libgflags-dev

### How to build
Go to the top directory of the project.
    
    mkdir build
    cmake --build build --config Debug --target craft

Then, you get craft library **libcraft.a** and three extra librares
**libsds.a**,**liblmdb.a**,**libsimple_logger.a** inside build directory.

Also, the raft simulator binary **basicsimulator** is there. The simulator is a driving environment for testing the raft algorithm.

### Run simulator
First, clean temp directories from the previous execution.

    sh removelmdb.sh

Then, run command following in a shell. 

    sudo build/basicsimulator --nodeCount=3 --testTime=40 --rebootRate=3 --enableCheckLiveness=false   --msgDropRate=60 --msgDuplicateRate=60 --msgDelayRate=70 --partitionRate=60 --memberChangeRate=30

Or, run it in the gdb environment for tracing errors

    sudo gdb  --args  build/basicsimulator --nodeCount=3 --testTime=40 --rebootRate=3 --enableCheckLiveness=false   --msgDropRate=60 --msgDuplicateRate=60 --msgDelayRate=70 --partitionRate=60 --memberChangeRate=30

Usually, the simulator ouputs statistics following every 30 seconds.
```
loop18 ======================================================================
0 : 7 (role Follower) rebootCount 3 nodeCount 13 term 834 votedFor -1 commitIndex 3613 lastApplied 3613 lastLogIndex 3662 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 2373 election 7171 heartbeat 1500 
1 : 4 (role Candidate) rebootCount 2 nodeCount 13 term 836 votedFor 4 commitIndex 3557 lastApplied 3557 lastLogIndex 3629 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 949 election 6304 heartbeat 1500 
2 : 6 (role Candidate) rebootCount 3 nodeCount 13 term 834 votedFor 6 commitIndex 3557 lastApplied 3557 lastLogIndex 3635 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 797 election 7406 heartbeat 1500 
3 : 15 (role Candidate) rebootCount 3 nodeCount 13 term 836 votedFor 15 commitIndex 3613 lastApplied 3613 lastLogIndex 3682 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 2169 election 6826 heartbeat 1500 
4 : 8 (role Candidate) rebootCount 4 nodeCount 13 term 834 votedFor 8 commitIndex 3613 lastApplied 3613 lastLogIndex 3635 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 3616 election 6403 heartbeat 1500 
5 : 2 (role Candidate) rebootCount 4 nodeCount 13 term 835 votedFor 2 commitIndex 3613 lastApplied 3613 lastLogIndex 3669 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 4769 election 6647 heartbeat 1500 
6 : 1 (role Follower) rebootCount 3 nodeCount 13 term 836 votedFor -1 commitIndex 3557 lastApplied 3557 lastLogIndex 3655 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 4945 election 6466 heartbeat 1500 
7 : 13 (role Follower) rebootCount 3 nodeCount 13 term 836 votedFor 15 commitIndex 3557 lastApplied 3557 lastLogIndex 3621 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 2333 election 6171 heartbeat 1500 
8 : 10 (role Follower) rebootCount 3 nodeCount 13 term 836 votedFor 15 commitIndex 3613 lastApplied 3613 lastLogIndex 3662 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 0 election 7459 heartbeat 1500 

0 : 3 (role Follower) rebooting timeleft 4 s rebootCount 3 nodeCount 1 term 816 votedFor 19 commitIndex 0 lastApplied 0 lastLogIndex 0 lastLogTerm 0 lastConfigEntryIndex -1 lastNoopEntryIndex 0 timeGone 0 election 7042 heartbeat 1500 
1 : 19 (role Follower) rebooting timeleft 40 s rebootCount 3 nodeCount 13 term 828 votedFor 19 commitIndex 3500 lastApplied 3500 lastLogIndex 3500 lastLogTerm 10 lastConfigEntryIndex 3322 lastNoopEntryIndex 240 timeGone 0 election 6003 heartbeat 1500 
2 : 17 (role Follower) rebooting timeleft 2 s rebootCount 3 nodeCount 13 term 835 votedFor 15 commitIndex 3557 lastApplied 3557 lastLogIndex 3655 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 0 election 7078 heartbeat 1500 
3 : 5 (role Follower) rebooting timeleft 30 s rebootCount 4 nodeCount 13 term 836 votedFor 5 commitIndex 3613 lastApplied 3613 lastLogIndex 3669 lastLogTerm 10 lastConfigEntryIndex 3616 lastNoopEntryIndex 240 timeGone 0 election 7059 heartbeat 1500 

loop18 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

```

You will get more hints about the usage of the simulator in testcmd.txt, basicsimulator.h and basicsimulator.cpp

# Basic docs
## Raft Consensus Algorithms
* [The summary of the Raft](https://github.com/daviszhen/raft/wiki/The-summary-of-the-Raft)
* [The procedure of the Leader in Raft](https://github.com/daviszhen/raft/wiki/The-procedure-of-the-Leader-in-Raft)
* [The procedure of the Follower in Raft](https://github.com/daviszhen/raft/wiki/The-procedure-of-the-Follower-in-Raft)
* [The procedure of the Candidate in Raft](https://github.com/daviszhen/raft/wiki/The-procedure-of-the-Candidate-in-Raft)
* [Cluster Membership changes JointConsensus Algorithm](https://github.com/daviszhen/raft/wiki/Cluster-Membership-changes---JointConsensus-Algorithm)
* [Cluster Membership changes Single Server Change Algorithm](https://github.com/daviszhen/raft/wiki/Cluster-Membership-changes-Single-Server-Change-Algorithm)

## Simulator
The simulator is a kind of test framework that drives the raft library for tests and demonstrations.

Recently, I promote the basic structure for the simulator which include these features:

1.  liveness check;
2.  election safety check;
3.  log matching check;
4.  last log index validity check;
5.  entry index monotonicity check;
6.  committed entries deleted check;
7.  committed entries deleted at begin check;
8.  leader append-only check;

some tactics for supporting features above:
1.  dynamic change the count of nodes;
2.  random drive time;
3.  the client messages delivery;
4.  drop messages;
5.  duplicate messages;
6.  delay messages;
7.  partition;
8.  node reboot;
9.  fast test and log control;

## Formal verification
The authors of the raft have suggested a TLA+ specification for the raft.
I also add some comments and complement some lemmas from the authors' papers
into the original TLA+ specification.

[TLA+ specification for raft consensus algorithm](https://github.com/daviszhen/raft.tla)

## Third party library
### string operations
- [sds - Simple Dynamic Strings library for C](https://github.com/antirez/sds)
    
### log entries persistence
- [lmdb - LIGHTNING MEMORY-MAPPED DATABASE](https://github.com/LMDB/lmdb/tree/mdb.master/libraries/liblmdb)

### options operations
- [gflags](https://github.com/gflags/gflags)

### log 
- [simple_logger](https://github.com/greensky00/simple_logger)

