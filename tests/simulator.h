#ifndef __SIMULATOR_H__
#define __SIMULATOR_H__

#include <cstdint>
#include <deque>
#include <random>
#include <set>
#include <chrono>
#include <unordered_map>

#include <gflags/gflags.h>

#include "raft.h"
#include "lmdbwrapper.h"

namespace Simulator{

//集群中的节点数
DECLARE_int32(nodeCount);
//最大增加的节点数，用于用于配置变更测试
DECLARE_int32(maxAdditionCount);
//随机驱动时间
DECLARE_bool(enableRandomPeriod);
//client给Raft投递数据的速度
DECLARE_int32(clientSubmitDataRate);
//消息丢失率
DECLARE_int32(msgDropRate);
//消息重复率
DECLARE_int32(msgDuplicateRate);
//消息延迟率
DECLARE_int32(msgDelayRate);
//网络分区发生率
DECLARE_int32(partitionRate);
//成员变更发生率
DECLARE_int32(memberChangeRate);
//开启集群活性检测
DECLARE_bool(enableCheckLiveness);
//lmdb数据库的路径
DECLARE_string(lmdbPath);
//lmdb数据库内存映射大小,MB
DECLARE_int32(lmdbMMSize);
//raft状态数据库名
DECLARE_string(lmdbRaftStates);
//raft entries数据库名
DECLARE_string(lmdbRaftEntries);
//节点重启率
DECLARE_int32(rebootRate);
//最小重启间隔(距离上次重启后的时间，单位秒)
DECLARE_int32(minRebootInterval);

enum NodeStatus{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

enum MsgType{
    NONE_TYPE,
    ECHO,
    ECHO_RESP,
    REQUESTVOTE,
    REQUESTVOTE_RESP,
    APPENDENTRIES,
    APPENDENTRIES_RESP,
    ADDREMOVESERVER,
    ADDREMOVESERVER_RESP
};

union MsgData{
    RequestVoteRequest rvReq;
    RequestVoteResponse rvResp;
    AppendEntriesRequest aeReq;
    AppendEntriesResponse aeResp;
    AddRemoveServerRequest arsReq;
    AddRemoveServerResponse arsResp;
};

class Node;

class Msg{
public:
    Msg():readyTime(){
        memset(this,0,sizeof(Msg));
    }
    Msg(const Msg& o){
        if(this == &o){
            return;
        }
        memset(this,0,sizeof(Msg));
        this->type = o.type;
        this->data = o.data;
        this->readyTime = o.readyTime;
        if(this->type == APPENDENTRIES){
            if(o.data.aeReq.entryCount > 0){
                this->data.aeReq.entries = nullptr;
                this->data.aeReq.entries = new (std::nothrow) Entry[o.data.aeReq.entryCount];
                for(int i=0;i<o.data.aeReq.entryCount;++i){
                    copyEntryTo(o.data.aeReq.entries+i,this->data.aeReq.entries+i);
                }
            }else{
                this->data.aeReq.entries = nullptr;
                this->data.aeReq.entryCount = 0;
            }
        }
        this->sender = o.sender;
        this->recver = o.recver;
    }
    ~Msg(){
        if(type == APPENDENTRIES && data.aeReq.entries != nullptr && data.aeReq.entryCount > 0){
            for(int i=0;i<data.aeReq.entryCount;++i){
                Entry* ent = data.aeReq.entries+i;
                if(ent){
                    deleteEntry(ent);
                }
            }
            delete [] data.aeReq.entries;
            sender = nullptr;
            recver = nullptr;
        }
    }
    enum MsgType type;
    union MsgData data;
    //投递时间，用于消息delay或重排序
    std::chrono::steady_clock::time_point readyTime;
    Node* sender;
    Node* recver;
private:
    Msg& operator=(const Msg&) = delete;
};

/**
 * 集群模拟器
 */
class Cluster{
public:
    Cluster();
    ~Cluster();

public:
    /**
     * 将新节点添加到集群中
     * 成功返回 true，结点加入执行队列
     * 否则false，此时重启队列，删除队列，执行队列中已有此结点。
     */
    bool addNode(Node*);

    /**
     * 从集群中删除节点
     */
    void removeNode(Node*);

    /**
     * 此节点可执行吗?
     */
    bool nodeIsActive(Node*);

    /**
     * 此结点在重启吗
     */
    bool nodeIsRebooting(Node*);

    /**
     * 此结点待删除吗?
     */
    bool nodeIsDeleting(Node*);

    /**
     * 从执行队列中删除节点，将节点加入待删除队列
     */
    void removeNodeFromActiveQueue(Node*);

    /**
     * 从重启队列中删除节点，将节点加入待删除队列
     */
    void removeNodeFromRebootingQueue(Node*);

    /**
     * 从待删除队列中，清空节点
     */
    void removeNodeFromDeletingQueue();

    /**
     * 用nodeId从执行队列中取节点
     */
    Node* getNodeFromActiveQueue(uint64_t);

    /**
     * 用nodeId从重启队列中取节点
     */
    Node* getNodeFromRebootingQueue(uint64_t);

    /**
     * 用nodeId从待删除队列中取节点
     */
    Node* getNodeFromDeletingQueue(uint64_t);

    /**
     * 客户端将<key,value>提交到集群
     * 集群给客户端提供的接口
     */
    void submitDataIntoCluster(int,int);

    /**
     * 集群核心驱动器。驱动节点执行。
     */
    void core();

    /**
     * 投递消息给各个节点
     */
    void dispatch();

    /**
     * 获取下一个节点Id
     */
    uint64_t getNextNodeId();

    /**
     * 打印结点信息
     */
    void printNodes();

    /**
     * 打印重启节点信息
     */
    void printRebootingNodes();

    /**
     * 在集群内部，发送者发送消息给接收者
     */
    void sendMsg(Node*,Node*,Msg*);

    /**
     * 创建随机分区
     * 随机取两个节点A和B，并将A和B，放在一个独立分区。相当于断开A到B之间的单向连接。
     */
    void addPartition();

    /**
     * 随机删除分区
     * 随机取出分区，并将A到B的单向连接重新连接起来。
     */
    void removePartition();

    /**
     * 向Raft中添加Server
     */
    void addServerIntoRaft();

    /**
     * 从Raft中删除Server
     */
    void removeServerFromRaft();

    /**
     * 随机重启一个节点
     */
    void rebootNode();

    int getLastAppliedLogIndex()const{
        return lastAppliedLogIndex;
    }
    
    int getLastAppliedLogClock()const{
        return lastAppliedLogClock;
    }
    
    void setLastAppliedLogIndex(int v){
        lastAppliedLogIndex = v;
    }
    
    void setLastAppliedLogClock(int v){
        lastAppliedLogClock = v;
    }
    
    void setLivenessCheckThreshold(int v){
        livenessCheckThreshold = v;
    }

    int getClusterClock(){
        return clusterClock;
    }

    void setClusterClock(int v){
        clusterClock = v;
    }
private:

    /**
     * 投递消息
     */
    void deliver(Msg*);

    /**
     * 删除消息
     */
    void deleteMsg(Msg*);

public:
    /**
     * 检测选举安全
     * 
     * Election Safety: at most one leader can be elected in a given term. §5.2
     */
    void checkElectionSafety();

    /**
     * 检测日志匹配和状态机安全
     * 
     * Log Matching: if two logs contain an entry with the same index and term, 
     * then the logs are identical in all entries up through the given index. §5.3
     * 
     * State Machine Safety: if a server has applied a log entry at a given index 
     * to its state machine, no other server will ever apply a different log entry 
     * for the same index. §5.4.3
     */
    void checkLogMatching(const RaftInterfaceType,const Entry*);

    /**
     * 检测lastLogIndex处是否有Entry
     */
    void checkLastLogIndexValidity();

    /**
     * 检测新增的Entry的index和term是否是单调性
     * 
     */
    void checkEntryIndexMonotonicity(const RaftInterfaceType,const Entry*);

    /**
     * 检测已committed的Entry，不能被从日志尾部删除
     */
    void checkCommittedEntriesDeleted(const RaftInterfaceType,const Entry*);

    /**
     * 检测只有已committed的Entry，才能被从日志头部删除
     */
    void checkCommittedEntriesDeletedAtBegin(const RaftInterfaceType,const Entry*);

    /**
     * 检测系统的活性
     * 系统在工作，但是数据提交不成功
     */
    void checkLiveness();

    /**
     * Leader不会覆盖或删除其日志中的entry。只追加新entry。
     * Leader Append-Only:a leader never overwrites or deletes entries in its log;
     * it only appends new entries. §5.3
     */
    void checkLeaderAppendOnly(const RaftInterfaceType);

private:
    //节点
    std::deque<Node*> nodes;
    //待删除的节点
    std::deque<Node*> deletingNodes;
    //重启中的节点
    std::deque<Node*> rebootingNodes;
    //消息队列
    std::deque<Msg*> messages;
    //网络分区
    std::set<std::pair<Node*,Node*>> partitions;
    //所有节点集合，方便查询
    std::unordered_map<uint64_t,Node*> nodeId2Nodes;

    //节点编号计数器
    static uint64_t nodeId;

    //随机数构建
    std::random_device rd;  // 将用于为随机数引擎获得种子
    std::mt19937 gen; // 以播种标准 mersenne_twister_engine
    std::uniform_int_distribution<> dis100;//生成[1,100]间的随机数
    std::uniform_int_distribution<> dis1000;//生成[1,1000]间的随机数

    //系统活性检测
    //最新被应用的日志索引
    int lastAppliedLogIndex;
    //最新被应用的日志时刻
    int lastAppliedLogClock;
    //系统活性检测阈值
    int livenessCheckThreshold;
    //时刻
    int clusterClock;
public:
    Interface raftInterfaces;
    sds raftConfig;
private:
    Cluster(const Cluster&) = delete;
    Cluster& operator=(const Cluster&) = delete;
};

/**
 * 通信节点
 */
class Node{
public:
    Node();
    Node(uint64_t);
    ~Node();

public:
    /**
     * 节点驱动器。驱动Raft执行N个时间单位。
     */
    void core(int);

    /**
     * 设置节点所在的集群
     */
    void setNodeCluster(Cluster* cluster){
        this->cluster = cluster;
    }

    /**
     * 将Entry放入状态机
     */
    void putEntryIntoMachine(const Entry* entPtr){
        assert(entPtr!=nullptr);
        assert(machineLog.empty() || machineLog.back().term <= entPtr->term);
        machineLog.push_back(Entry());
        copyEntryTo(entPtr,&machineLog.back());
    }

    /**
     * 持久化Raft实例状态
     * 将Raft状态存入lmdb中
     */
    void persistRaftStates(const RaftInterfaceType);

    /**
     * 持久化Entry
     * 将Entry存入lmdb中
     */
    void addEntryAtEnd(const RaftInterfaceType,const Entry*);

    /**
     * 删除最后存入lmdb的Entry
     */
    void deleteEntryAtEnd(const RaftInterfaceType,const Entry*);

    /**
     * 删除最先存入lmdb的Entry
     */
    void deleteEntryAtBegin(const RaftInterfaceType,const Entry*);

    /**
     * 从lmdb中加载Entries，并投入Raft实例中
     */
    void loadLogEntries(RaftInterfaceType);

    /**
     * 从lmdb中加载状态，并投入Raft实例中
     */
    void loadStates(RaftInterfaceType);

    /**
     * 重启Node，实际上只是重启Raft实例
     */
    void rebootRaft();
public:
    //也raft实例的id，从集群中申请
    uint64_t nodeId;
    Raft* raft;
    enum NodeStatus connectionStatus;
    //所在的集群
    Cluster* cluster;
    //状态机日志
    std::deque<Entry> machineLog;
    //状态机
    //lmdb数据库路径
    std::string lmdbPath;
    //lmdb数据库环境
    MDB_env *raftMdbEnv;
    //lmdb数据库，存储Raft相关信息
    //存储currentTerm和votedFor等
    MDB_dbi raftStates;
    //存储Raft日志entries
    MDB_dbi raftEntries;
    //活动时间点，加入集群调度的时间点，用于Raft重启测试
    std::chrono::steady_clock::time_point readyTime;
    //重启次数
    int rebootCount;
private:
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

/**
 * 在Cluster中发送RequestVoteRequest请求。
 * 返回0-成功
 */
int sendRequestVoteRequestInCluster(RaftInterfaceType raftPtr,RaftNodeInterfaceType nodePtr,UserDataInterfaceType raftUserData,const RequestVoteRequest* reqPtr);

/**
 * 在Cluster中发送AppendEntriesRequest请求。
 * 返回0-成功
 */
int sendAppendEntriesRequestInCluster(RaftInterfaceType raftPtr,RaftNodeInterfaceType nodePtr,UserDataInterfaceType raftUserData,const AppendEntriesRequest* reqPtr);

/**
 * 向日志中添加Entry
 * 返回0-成功
 */
int addingEntryInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * 从日志尾部删除Entry
 * 返回0-成功
 */
int deletingEntryAtEndInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * 从日志头部删除Entry
 * 返回0-成功
 */
int deletingEntryAtBeginInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * Raft实例应用Entry到状态机函数接口
 * 返回0-成功
 */
int applyLogInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*);

/**
 * 获取配置Entry中的NodeId
 * 返回0-成功
 */
int getConfigEntryNodeIdInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*,UserDataInterfaceType);

/**
 * 配置变更通知
 * 返回0-成功
 */
int membershipChangeEventInCluster(RaftInterfaceType,UserDataInterfaceType,Entry*,UserDataInterfaceType);

/**
 * 在Raft实例即将转为Leader前的钩子函数
 */
void onConversionToLeaderBeforeInCluster(RaftInterfaceType);

/**
 * 在Raft实例刚转为Leader后的钩子函数
 */
void onConversionToLeaderAfterInCluster(RaftInterfaceType);

/**
 * 持久化Raft实例的状态
 */
void persistStateInCluster(const RaftInterfaceType);
}

#endif