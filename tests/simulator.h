#ifndef __SIMULATOR_H__
#define __SIMULATOR_H__

#include <cstdint>
#include <deque>

#include "raft.h"

namespace Simulator{

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
    Msg(){
        memset(this,0,sizeof(Msg));
    }
    ~Msg(){
        if(type == APPENDENTRIES && data.aeReq.entries != nullptr){
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
    Node* sender;
    Node* recver;
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
     */
    void addNode(Node*);

    /**
     * 从集群中删除节点
     */
    void removeNode(Node*);

    /**
     * 用nodeId从集群中取节点
     */
    Node* getNodeByNodeId(uint64_t);

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
     * 在集群内部，发送者发送消息给接收者
     */
    void sendMsg(Node*,Node*,Msg*);

private:

    /**
     * 投递消息
     */
    void deliver(Msg*);

    /**
     * 删除消息
     */
    void deleteMsg(Msg*);

private:
    //节点
    std::deque<Node*> nodes;
    //消息队列
    std::deque<Msg*> messages;

    //节点编号计数器
    static uint64_t nodeId;

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

public:
    //也raft实例的id，从集群中申请
    uint64_t nodeId;
    Raft* raft;
    enum NodeStatus connectionStatus;
    //所在的集群
    Cluster* cluster;
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
}

#endif