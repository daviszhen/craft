#ifndef __RAFT_H__
#define __RAFT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "sds.h"

#define raftAssert(expr) assert((expr))

//内存分配函数
extern void *(*raftMalloc) (size_t);
extern void *(*raftCalloc) (size_t, size_t);
extern void *(*raftRealloc) (void *, size_t);
extern void (*raftFree) (void *);

//Raft 三种状态
enum Role{
    Follower,
    Candidate,
    Leader
};

#ifndef IP_LEN
#define IP_LEN (46)
#endif

//==========================Raft日志相关================================
/**
 * 节点状态：在线且无投票权，在线且有投票权，下线，
 * 
 * 状态转化
 * 
 * 新增节点：在线且无投票权
 * 
 * 在线且无投票权 -> 在线且有投票权
 */

/*节点投票*/
#define RAFT_NODE_VOTED                 (1  <<  0)
/*节点有投票权*/
#define RAFT_NODE_CAN_VOTE              (1  <<  1)
/*节点在线*/
#define RAFT_NODE_ACTIVE                (1  <<  2)
/*节点的投票权，已经COMMITTED*/
#define RAFT_NODE_CAN_VOTE_COMMITTED    (1  <<  3)
/*节点添加，已经COMMITTED*/
#define RAFT_NODE_ADD_COMMITTED    (1  <<  4)

enum EntryType{
    NO_OP,
    DATA,
    LOAD_CONFIG,
    ADD_NODE,
    ENABLE_VOTE,
    DISABLE_VOTE,
    REMOVE_NODE
};

typedef struct
{
    int term;
    int index;
    enum EntryType type;
    //字节数据指针，和字节长度
    int dataLen;
    sds data;
} Entry;

typedef struct
{
    int capacity;//数组容量元素个数
    int size;//实际元素个数

    //用于压缩日志
    int lastIncludedIndex;
    int lastIncludedTerm;

    int front;//第一个元素的下标
    int back;//最后一个元素的下标
    Entry* entries;
    void* raft;

    //最新一条配置Entry的index
    int lastConfigEntryIndex;
    //最新一条NO_OP Entry的index
    int lastNoopEntryIndex;
} Log;

//===========================Raft RPC相关============================

typedef struct
{
    int term;
    uint64_t candidateId;
    int lastLogIndex;
    int lastLogTerm;
} RequestVoteRequest;

typedef struct
{
    int term;
    //1 for true; 0 for false
    int voteGranted;
    //请求中的term
    int requestTerm;
} RequestVoteResponse;

typedef struct
{
    int term;
    uint64_t leaderId;
    int prevLogIndex;
    int prevLogTerm;
    int leaderCommit;
    int entryCount;
    Entry* entries;
} AppendEntriesRequest;

typedef struct
{
    int term;
    //1 for 成功
    int success;
    int conflictTerm;
    int firstIndexOfConflictTerm;
    //请求中的
    int requestTerm;
    int prevLogIndex;
    int entryCount;
} AppendEntriesResponse;

enum AddRemoveServerMethod{
    ADD,
    REMOVE
};

enum AddRemoveServerStatus{
    CONFIG_NONE /*没有发送过请求*/,
    OK,
    NOT_LEADER /*不是leader*/,
    TIMEOUT,
    SERVER_IN_CONFIG_ALREADY /*server在Cold配置中*/,
    SERVER_NOT_IN_CONFIG /*server不在Cold配置中*/,
    UNKNOWN_METHOD /*Add or Remove之外的方法*/,
    WAIT_PREVIOUS_CONFIG_COMMITTED /*leader还未commit之前的配置(Cold,Cnew or 其它)*/,
    WAIT_NO_OP_ENTRY_COMMITTED_IN_COLD /*leader在currentTerm中没有commit过任何entry。要先commit一个entry*/,
    WAIT_CNEW_ENTRY_COMMITED_IN_CNEW /*在Cnew配置中，commit Cnew*/,
    WAIT_PENDING_REQUEST_ANSWERED /*leader已commit之前的配置(Cold,Cnew or 其它)，但是还未答复请求*/
};

typedef struct{
    enum AddRemoveServerMethod method;
    char ip[IP_LEN];
    int port;
    uint64_t nodeId;
    sds userData;
    int userDataLen;
} AddRemoveServerRequest;

typedef struct{
    enum AddRemoveServerStatus status;
    char leaderIp[IP_LEN];
    int leaderPort;
    uint64_t leaderNodeId;
    sds leaderUserData;
    int leaderUserDataLen;
} AddRemoveServerResponse;

//============================Raft 实例相关=====================================

struct _raft;
struct _raftnode;

/**
 * 用于Raft对外提供通信的接口 
 */
typedef struct _raft* RaftInterfaceType;
typedef struct _raftnode* RaftNodeInterfaceType;
typedef void* UserDataInterfaceType;

/**
 * 发送RequestVoteRequest的函数接口。
 * 返回0-成功
 */
typedef int(*sendRequestVoteRequestInterface)(RaftInterfaceType,RaftNodeInterfaceType,UserDataInterfaceType,const RequestVoteRequest*);

/**
 * 发送AppendEntriesRequest的函数接口。
 * 返回0-成功
 */
typedef int(*sendAppendEntriesRequestInterface)(RaftInterfaceType,RaftNodeInterfaceType,UserDataInterfaceType,const AppendEntriesRequest*);

/**
 * 发送AddRemoveServerRequest的函数接口
 * 返回0-成功
 */
//typedef int(*sendAddRemoveServerRequestInterface)(RaftInterfaceType,RaftNodeInterfaceType,UserDataInterfaceType,const AddRemoveServerRequest*);

/**
 * 向机器发送AddRemoveServer请求的函数接口
 * 请求中的机器加入到机器(nodeId和userdata)所在集群中（或从中删除)
 * 返回0 - 成功
 */
typedef int(*sendAddRemoveServerRequestInterface)(RaftInterfaceType,uint64_t,UserDataInterfaceType,const AddRemoveServerRequest*);

/**
 * 日志事件钩子函数接口
 * 返回0 - 成功
 */
typedef int(*logEventHookInterface)(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * 日志事件钩子函数扩展接口
 * 带上了一个扩展参数
 * 返回0 - 成功
 */
typedef int(*logEventHookExtendInterface)(RaftInterfaceType,UserDataInterfaceType,Entry*,UserDataInterfaceType);

/**
 * 发送AddRemoveServer响应的函数接口
 */
typedef void(*sendAddRemoveServerResponseInterface)(RaftInterfaceType,UserDataInterfaceType,const AddRemoveServerResponse*);

/**
 * RaftNode中UserData数据转成字符串函数接口。
 * 字符串输出格式：个数(不含此字段) 字段1 字段2 字段3 ......
 * 注意：第一个字段是数字，表示后续字段有多少个。 字段内部无(空格，tab，回车，换行等空白符)。字段间用空格符隔开。
 */
typedef void(*raftNodeUserDataToStringInterface)(const UserDataInterfaceType,sds*);

/**
 * RaftNode中字符串转成UserData数据函数接口。
 * 字符串输入格式，字符串数组，字符串个数：个数(不含此字段) 字段1 字段2 字段3 ......
 * 注意：第一个字段是数字，表示后续字段有多少个。 字段内部无(空格，tab，回车，换行等空白符)。字段间用空格符隔开。
 * 返回：消耗掉的字符串个数
 */
typedef int(*raftNodeStringToUserDataInterface)(const sds*,int,UserDataInterfaceType*);

/**
 * AddRemoveServer中UserData数据转成字符串函数接口。
 * 字符串输出格式：个数(不含此字段) 字段1 字段2 字段3 ......
 * 注意：第一个字段是数字，表示后续字段有多少个。 字段内部无(空格，tab，回车，换行等空白符)。字段间用空格符隔开。
 */
typedef void(*addRemoveServerUserDataToStringInterface)(const UserDataInterfaceType,sds*);

/**
 * AddRemoveServer中字符串转成UserData数据函数接口。
 * 字符串输入格式，字符串数组，字符串个数：个数(不含此字段) 字段1 字段2 字段3 ......
 * 注意：第一个字段是数字，表示后续字段有多少个。 字段内部无(空格，tab，回车，换行等空白符)。字段间用空格符隔开。
 * 返回：消耗掉的字符串个数
 */
typedef int(*addRemoveServerStringToUserDataInterface)(const sds*,int,UserDataInterfaceType*);

/**
 * Raft中UserData数据转成字符串函数接口。
 * 输出格式：个数(不含此字段) 字段1 字段2 字段3 ......
 * 注意：第一个字段是数字，表示后续字段有多少个。 字段内部无(空格，tab，回车，换行等空白符)。字段间用空格符隔开。
 */
typedef void(*raftUserDataToStringInterface)(const UserDataInterfaceType,sds*);

/**
 * Raft实例转为Leader的回调函数
 */
typedef void(*onConversionToLeaderHookInterface)(const RaftInterfaceType);

/**
 * 持久化Raft实例状态的回调函数
 */
typedef void(*persistStateHookInteface)(const RaftInterfaceType);

//Raft实例对外通信接口
typedef struct{
    sendRequestVoteRequestInterface sendRequestVoteRequest;
    sendAppendEntriesRequestInterface sendAppendEntriesRequest;
    sendAddRemoveServerRequestInterface sendAddRemoveServerRequest;
    sendAddRemoveServerResponseInterface sendAddRemoveServerResponse;
    raftNodeUserDataToStringInterface raftNodeUserDataToString;
    raftNodeStringToUserDataInterface raftNodeStringToUserData;
    addRemoveServerUserDataToStringInterface addRemoveServerUserDataToString;
    addRemoveServerStringToUserDataInterface addRemoveServerStringToUserData;
    raftUserDataToStringInterface raftUserDataToString;
    logEventHookInterface addingEntry;//向日志添加Entry时的钩子函数
    logEventHookInterface deletingEntryAtEnd;//从日志尾部删除Entry时的钩子函数
    logEventHookInterface deletingEntryAtBegin;//从日志头部删除Entry时的钩子函数
    logEventHookInterface applyEntry;//应用Entry到状态机的钩子函数
    logEventHookExtendInterface getConfigEntryNodeId;//获取配置中机器的noideId的钩子函数
    logEventHookExtendInterface membershipChangeEvent;//增加或删除RaftNode的钩子函数
    onConversionToLeaderHookInterface onConversionToLeaderBefore;//在Raft实例即将转为Leader前的钩子函数
    onConversionToLeaderHookInterface onConversionToLeaderAfter;//在Raft实例刚转为Leader后的钩子函数
    persistStateHookInteface persistState;//持久化Raft实例状态的钩子函数
}Interface;

//参与raft协议的机器
typedef struct _raftnode
{
    int nextIndex;
    int matchIndex;
    int appendEntriesSendTime;

    //机器标识
    uint64_t nodeId;
    //与机器有关的其它信息
    void* userData;
    
    //在选举时，是否投票 1-投票 0-不投票
    int isVote;
    //节点状态
    int status;

    //对外接口
    Interface interfaces;
}RaftNode;

//Raft实例
typedef struct _raft{
    //持久状态
    int currentTerm;
    uint64_t votedFor;
    Log * log;

    //可变状态
    int commitIndex;
    int lastApplied;

    //状态
    int role;

    //定时器超时时间
    int electionTimeoutBegin;
    int electionTimeoutEnd;
    int electionTimeout;
    int heartbeatTimeout;

    //流量控制
    //每个AppendEntries中包含的Entry的最大个数
    int maxEntryCountInOneAppendEntries;
    //两个连续AppendEntries包之间的最大时间间隔
    int maxAppendEntriesSendInterval;

    //计时器
    int timeGone;

    //参与raft协议的机器信息
    RaftNode **nodeList;
    int nodeCount;

    /*
    注意:在leader将其自身从集群中删除时，会出现leader管理一个
    不包含自己的集群的情况。因此，存在nodeId还是很有必要的。
    */
    uint64_t nodeId;
    RaftNode *leaderNode;//是leader的机器。没有时为NULL
    RaftNode *selfNode;//此Raft实例所在的机器

    //Raft leader接收AddRemoveServer Request
    //pending的AddRemoveServer Request
    void* pendingAddRemoveServerRequest;
    void* pendingAddRemoveServerPeer;

    //Raft AddRemoveServer请求状态
    int addRemoveServerStatus;
    //Add or Remove a server
    int addRemoveServerMethod;

    //leader在currentTerm中已经commit过entry的个数。在AddServer和RemoveServer时，会用到
    int countOfLeaderCommittedEntriesInCurrentTerm;

    //每次应用到状态机Entry的个数
    int maxCountOfApplyingEntry;
    
    //对外接口
    Interface interfaces;

    //调用者传入的用户数据
    UserDataInterfaceType userData;
} Raft;

//==================================日志相关===================================
/**
 * 获取n个Entry的数组
 */
Entry* getNewEntry(int);

/**
 * 删除Entry内部数据。不删除Entry本身。
 */
void deleteEntry(Entry*);

/**
 * Entry深度复制
 */
void copyEntryTo(const Entry*,Entry*);

/**
 *获取Entry的存储空间大小。包括内部数据长度 
 */
int getEntrySpaceLength(const Entry*);

/**
 * 是否是投票权配置变更Entery
 * 返回 1 是，返回 0 否
 */
int isVoteRightConfigChangeEntry(const int);

/**
 * 是否是配置变更Entery
 * 包括投票权配置变更
 * 返回 1 是，返回 0 否
 */
int isConfigChangeEntry(const int);

/**
 * 如果有必要，两倍扩展Entry存储空间.
 */
void expand(Log*);

/**
 * 分配新的日志
 */
Log* getNewLog();

/**
 * 删除日志，释放空间
 */
void deleteLog(Log*);

/**
 * 日志的长度。
 * 注意：当做过快照后，日志的长度会大于存储元素的个数。
 */
int logLength(const Log*);

/**
 * 返回lastIncludedIndex值
 */
int logLastIncludedIndex(const Log*);

/**
 * 返回lastIncludedTerm值
 */
int logLastIncludedTerm(const Log*);

/**
 * 第一个元素的索引
 */
int logFirstLogIndex(const Log*);

/**
 * 最后一个元素的索引
 */
int logLastLogIndex(const Log*);

/**
 * 最后一个元素的term
 */
int logLastLogTerm(const Log*);

/**
 * 在索引处的日志
 */
Entry logAt(const Log*,int);

/**
 * 在日志尾部拼接一个Entry
 * 返回此Entry的index
 */
int logAppend(Raft*,Entry);

/**
 * 在Raft日志尾部拼接数据。内部将其转成Entry
 * 返回此Entry的index
 */
int logAppendData(Raft*,sds,int,enum EntryType);

/**
 * 在Raft日志尾部拼接NO_OP数据
 * 返回此Entry的index
 */
int logApppendNoop(Raft*);

/**
 * 从lastIncludedIndex到位置i有多少个元素
 */
int logCountFromBegin(const Log*,int);

/**
 * 删除尾部N个元素
 */
void logDeleteAtEnd(Raft*,int);

/**
 * 删除头部N个元素
 */
void logDeleteAtBegin(Raft*,int);

/**
 * 最新一条配置Entry的index
 */
int getLastConfigEntryIndex(Log*);

/**
 * 最新一条NO_OP Entry的index
 */
int getLastNoopEntryIndex(Log*);

//=================================================机器相关============================

/**
 * 机器(nodeId)在当前的机器配置中吗？
 */
int isNodeInConfig(Raft*,uint64_t);

/**
 * 从机器列表中构造配置字符串。每个机器配置之间用空格分开。并增加一个参数中的机器。
 * 返回字符串 nodeId [fields] 1 nodeId [fields] 1 nodeId [fields] 1....
 */
sds makeConfigStringWith(Raft*,uint64_t,UserDataInterfaceType);

/**
 * 从机器列表中构造配置字符串。每个机器配置之间用空格分开。并删除一个参数中的机器。
 * 返回字符串 nodeId [fields] 1/0 nodeId [fields] 1/0 nodeId [fields] 1/0 ....
 */
sds makeConfigStringWithout(Raft*,uint64_t/*,UserDataInterfaceType*/);

/**
 * 从机器列表中构造配置字符串。每个机器配置之间用空格分开
 * 返回字符串 nodeId [fields] 1 nodeId [fields] 1 nodeId [fields] 1....
 */
sds makeConfigString(Raft*);

/**
 * 从配置字符串(nodeId [fields] 1/0 nodeId [fields] 1/0 nodeId [fields] 1/0 ....)中，加载机器
 * Raft实例至少关联一个机器
 */
void loadConfigFromString(Raft*,const sds,int);

//================================Raft实例相关=================
Raft* getNewRaft();
void deleteRaft(Raft*);
void resetRaftTime(Raft*);
void addRaftTime(Raft*,int);
int getRaftTime(const Raft*);

/**
 * 在要与外部交互前，设置Raft的Interface和UserData。
 */
void setRaftInterface(Raft*,Interface*,UserDataInterfaceType);

/**
 * 设置投票votedFor
 */
void setRaftVotedFor(Raft*,uint64_t);

/**
 * 获得votedFor
 */
uint64_t getRaftVotedFor(const Raft*);

/**
 * 设置currentTerm
 */
void setRaftCurrentTerm(Raft*,int);

/**
 * 获得currentTerm
 */
int getRaftCurrentTerm(const Raft*);

/**
 * 设置commitIndex
 */
void setRaftCommitIndex(Raft*,int);

/**
 * 获得commitIndex
 */
int getRaftCommitIndex(const Raft*);

/**
 * Raft切换角色
 * 切换到Follower
 */
void onConversionToFollower(Raft*);

/**
 * Raft切换角色
 * 切换到Candidate
 */
void onConversionToCandidate(Raft*);

/**
 * Raft切换角色
 * 切换到Leader
 */
void onConversionToLeader(Raft*);

/**
 * Raft是leader吗？
 */
int isLeader(const Raft*);

/**
 * 获取majority的投票了吗？
 */
int getMajorityVote(const Raft*);

/**
 * 获取majority的个数
 */
int getMajority(const Raft*);

/**
 * 数量是否超过majority
 */
int beyondMajority(int,const Raft*);

/**
 * 获取能投票的节点个数
 */
int getCountOfRaftNodeActiveAndCanVote(const Raft*);

/**
 * 此raft实例的日志是否更新
 * 返回1 是；返回0 否
 */
int myLogIsNewer(const Log*,const RequestVoteRequest*);

/**
 * 
 * apply日志到状态机器
 */
void applyLog(Raft*,int);

/**
 * 默认应用日志到状态机函数
 */
int defaultApplyLog(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * 判断Entry的index是否被committed
 * 返回0 - 否； 其它 - 是
 */
int indexIsCommitted(Raft*,int);

/**
 * leader在currentTerm中有没有commit过一个entry
 * 返回0 - 否； 其它 - 是
 */
int isLeaderCommittedEntriesInCurrentTerm(Raft*);

/**
 * Raft协议的核心过程 
 */
void raftCore(Raft*);

//=====================================================
//Invariants in Raft
#define expect(expr) do{\
    if(!(expr)){\
        raftPanic("'%s' is false",#expr);\
    }\
}while(0)

/**
 * Raft保持基本的不变量
 */
void basicInvariants(const Raft*);

/**
 * RaftNode保持基本的不变量
 */
void raftNodeBasicInvariants(const Raft*);

/**
 * 检查必要的不变量
 */
void checkInvariants(const Raft*);

//===========================Raft机器相关==========================
/**
 * 用机器连接信息，机器id，用户自定义数据，创建一个新的机器节点。 
 */
RaftNode* getNewNode(uint64_t,void*);

void deleteRaftNode(RaftNode*);

/**
 * 在要与外部交互前，设置RaftNode的Interface。
 */
void setRaftNodeInterface(RaftNode*,Interface*);

/**
 * 从RaftNode中构造配置字符串。
 * 返回字符串。
 */
void getRaftNodeConfigString(RaftNode*,sds*);

/**
 * 复位nextIndex和matchIndex
 */
void resetRaftNodeIndex(RaftNode*,int,int);

/**
 * 设置节点投票
 * 
 */
void raftNodeSetVote(RaftNode*,int);

/**
 * 复位投票
 * 1-投票，0-不投票
 */
void resetRaftNodeVote(RaftNode*,int);

/**
 * 节点有投票吗
 */
int raftNodeHasVoted(RaftNode*);

/**
 * 设置节点投票权
 */
void raftNodeSetCanVote(RaftNode*,int);

/**
 * 节点有投票权吗
 */
int raftNodeHasCanVote(RaftNode*);

/**
 * 设置节点在线
 */
void raftNodeSetActive(RaftNode*,int);

/**
 * 节点在线吗
 * 
 */
int raftNodeHasActive(RaftNode*);

/**
 * 设置节点投票已committed
 * 
 */
void raftNodeSetCanVoteCommitted(RaftNode*,int);

/**
 * 节点投票权已committed
 * 
 */
int raftNodeHasCanVoteCommitted(RaftNode*);

/**
 * 设置节点添加已committed
 */
void raftNodeSetAddCommitted(RaftNode*,int);

/**
 * 节点添加已committed
 */
int raftNodeHasAddCommitted(RaftNode*);

void showRaftNode(const RaftNode*);

/**
 * 获取此Raft实例所在机器的id
 */
uint64_t getRaftNodeId(const Raft*);

/**
 * 向Raft协议中添加机器(nodeId,ip,port,userData)
 * 如果已经有此机器，返回NULL.
 * 如果是此Raft实例所在的机器。设置selfNode.
 */
RaftNode* addRaftNodeIntoConfig(Raft*,uint64_t,void*,int);

/**
 * 向Raft协议中添加机器(nodeId,ip,port,userData)。但此机器无投票权。
 * 如果已经有此机器，返回NULL.
 * 如果是此Raft实例所在的机器。设置selfNode.
 */
RaftNode* addRaftNodeIntoConfigWithoutCanVote(Raft*,uint64_t,void*,int);

/**
 * 从Raft的配置中删除机器
 * 如果没有，返回
 */
void removeRaftNodeFromConfig(Raft*,uint64_t);

/**
 * 用id从Raft中获取指定连接的机器
 * 如果没有，返回NULL
 */
RaftNode* getRaftNodeFromConfig(const Raft*,uint64_t);

/**
 * 设置leader 机器
 */
void setRaftLeaderNode(Raft*,RaftNode*);

/**
 * 向机器发送RequestVote请求。但是不发给自己。
 * 返回0 - 成功
 */
int sendRequestVote(Raft*,RaftNode*);

/**
 * 向机器发送AppendEntries请求。但是不发给自己。
 * Entry个数 == 0 - 心跳包
 *           > 0 - 复制包
 * 返回0 - 成功
 */
int sendAppendEntries(Raft*,RaftNode*,int);

/**
 * 获取AppendEntriesRequest请求的总数据长度，包括内部Entry总长度
 */
int getAppendEntriesRequestLength(const AppendEntriesRequest*);

/**
 * 向其它机器发送心跳。但是不发给自己。
 * Entry个数 == 0 - 心跳包
 *           > 0 - 复制包
 * 返回0 - 成功
 */
int sendHeartbeatsToOthers(Raft*);

/**
 * 向其它机器发送未同步的日志。但是不发给自己。
 * Entry个数 == 0 - 心跳包
 *           > 0 - 复制包
 * 返回0 - 成功
 */
int sendAppendEntriesToOthersIfNeed(Raft*);

/**
 * leader计算机器提交情况。并更新commitIndex
 * 
 */
void leaderUpdateCommitIndex(Raft*);

/**
 * 在follower,candidate,leader三种角色下，收到RequestVoteRequest的不同处理
 * 返回1 表示角色切换，再执行一次。
 */
int followerRecvRequestVote(Raft*,const RaftNode*,const RequestVoteRequest*,RequestVoteResponse*);
int candidateRecvRequestVote(Raft*,const RaftNode*,const RequestVoteRequest*,RequestVoteResponse*);
int leaderRecvRequestVote(Raft*,const RaftNode*,const RequestVoteRequest*,RequestVoteResponse*);
int recvRequestVote(Raft*,const RaftNode*,const RequestVoteRequest*,RequestVoteResponse*);

/**
 * 在follower,candidate,leader三种角色下，收到RequestVoteResponse的不同处理
 * 返回1 表示角色切换，再执行一次。
 */
int followerRecvRequestVoteResponse(Raft*,const RaftNode*,const RequestVoteResponse*);
int candidateRecvRequestVoteResponse(Raft*,const RaftNode*,const RequestVoteResponse*);
int leaderRecvRequestVoteResponse(Raft*,const RaftNode*,const RequestVoteResponse*);
int recvRequestVoteResponse(Raft*,const RaftNode*,const RequestVoteResponse*);

/**
 * 在follower,candidate,leader三种角色下，收到AppendEntriesRequest的不同处理
 * 返回1 表示角色切换，再执行一次。
 */
int followerRecvAppendEntriesRequest(Raft*,RaftNode*,const AppendEntriesRequest*,AppendEntriesResponse*);
int candidateRecvAppendEntriesRequest(Raft*,const RaftNode*,const AppendEntriesRequest*,AppendEntriesResponse*);
int leaderRecvAppendEntriesRequest(Raft*,const RaftNode*,const AppendEntriesRequest*,AppendEntriesResponse*);
int recvAppendEntriesRequest(Raft*,const RaftNode*,const AppendEntriesRequest*,AppendEntriesResponse*);

/**
 * 在follower,candidate,leader三种角色下，收到AppendEntriesResponse的不同处理
 * 返回1 表示角色切换，再执行一次。
 */
int followerRecvAppendEntriesResponse(Raft*,const RaftNode*,const AppendEntriesResponse*);
int candidateRecvAppendEntriesResponse(Raft*,const RaftNode*,const AppendEntriesResponse*);
int leaderRecvAppendEntriesResponse(Raft*,RaftNode*,const AppendEntriesResponse*);
int recvAppendEntriesResponse(Raft*,RaftNode*,const AppendEntriesResponse*);

/**
 * 客户端提交数据给Raft。
 * 返回0，表示成功讲数据放入日志（但不表示日志已经提交到集群）。
 *  输出参数，返回数据对应Entry的index，term
 * 返回其它，表示失败。
 */
int submitDataToRaft(Raft* raftPtr,sds data,int dataLen,enum EntryType type,int* index,int* term);

/**
 * 向Raft实例所在集群添加新机器(nodeId和其它信息)。本接口是给管理员调用的。
 * 调用者会被阻塞，直到得到答复才能返回。
 * 返回0 - 成功
 */
int addServer(Raft*,uint64_t,UserDataInterfaceType);

/**
 * 向Raft实例所在集群添加新机器(nodeId和其它信息)。本接口是给管理员调用的。
 * 管理员直接调用在Raft实例上，而不是通过通讯媒介到达Raft实例的。
 * 调用者会被阻塞，直到得到答复才能返回。
 * 返回0 - 成功
 */
int addServerRPC(RaftInterfaceType,uint64_t,UserDataInterfaceType);

/**
 * 向机器(ip和port)发送AddRemoveServer请求,将所在机器加入到机器(ip和port)所在集群中（或从中删除）。但是不发给自己。
 * 返回0 - 成功
 */
//int sendAddRemoveServerRequest(Raft*,const char*,int,enum AddRemoveServerMethod);

/**
 * 处理AddRemoveServer请求。
 * 返回0 - 成功
 */
int recvAddRemoveServerRequest(Raft*,UserDataInterfaceType,const AddRemoveServerRequest*,AddRemoveServerResponse*);

/**
 * 如果有pending的AddRemoveServerRequest，处理掉
 */
void processPendigAddRemoveServerRequest(Raft*);



//==============================================
int max(int,int);
int min(int,int);

//日志级别
#define RAFT_DEBUG 0
#define RAFT_VERBOSE 1
#define RAFT_NOTICE 2
#define RAFT_WARNING 3

//raft日志级别
extern int raftLogLevel;

//输出日志
void raftLog(int level,const char* fmt,...);
//终止程序
void raftPanic(const char* fmt,...);

#ifdef __cplusplus
}
#endif

#endif