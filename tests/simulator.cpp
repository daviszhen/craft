#include "simulator.h"
#include <cassert>
#include <new>
#include <random>

namespace Simulator{

uint64_t Cluster::nodeId = 0;

Cluster::Cluster():nodes(),messages(){}

Cluster::~Cluster(){
    for(auto msg: this->messages){
        delete msg;
    }

    for(auto node : this->nodes){
        delete node;
    }
}

void Cluster::addNode(Node* nodePtr){
    assert(nodePtr!=nullptr);
    for(auto it : nodes){
        if(it->nodeId == nodeId){
            return;
        }
    }
    nodes.push_back(nodePtr);
}

void Cluster::removeNode(Node*){}

Node* Cluster::getNodeByNodeId(uint64_t nodeId){
    for(auto node : this->nodes){
        if(node->nodeId == nodeId){
            return node;
        }
    }
    //一定在集群中
    assert(0);
    return nullptr;
}

void Cluster::submitDataIntoCluster(int key,int value){
    for(auto node : this->nodes){
        if(isLeader(node->raft)){
            sds data=sdsempty();
            data=sdscatfmt(data,"%i:%i",key,value);
            submitDataToRaft(node->raft,data,strlen(data),DATA,nullptr,nullptr);
            sdsfree(data);
        }
    }
}

void Cluster::core(){
    //随机数构建
    std::random_device rd;  // 将用于为随机数引擎获得种子
    std::mt19937 gen(rd()); // 以播种标准 mersenne_twister_engine
    std::uniform_int_distribution<> dis(1, 100);
    
    //生成随机数据
    if(dis(gen) < 70){
        submitDataIntoCluster(dis(gen),dis(gen));
    }
    //驱动节点执行
    for(auto node : this->nodes){
        node->core(100);
    }
}

void Cluster::dispatch(){
    std::deque<Msg*> oldMsgQueue;
    this->messages.swap(oldMsgQueue);

    for(auto msg: oldMsgQueue){
        deliver(msg);
    }
}

uint64_t Cluster::getNextNodeId(){
    return ++nodeId;
}

void Cluster::sendMsg(Node* sender,Node* recver,Msg* msg){
    assert(sender != nullptr && sender->raft != nullptr);
    assert(recver != nullptr && recver->raft != nullptr);
    msg->sender = sender;
    msg->recver = recver;
    this->messages.push_back(msg);
}

void Cluster::deliver(Msg* msg){
    assert(msg != nullptr);
    assert(msg->sender != nullptr);
    assert(msg->sender->raft != nullptr);
    assert(msg->recver != nullptr);
    assert(msg->recver->raft != nullptr);

    switch (msg->type)
    {
    case REQUESTVOTE:
        {
            Msg* respMsg = new (std::nothrow) Msg();
            assert(respMsg!=nullptr);
            respMsg->type = REQUESTVOTE_RESP;
            //从接收者Raft实例中获取发送者对应的RaftNode
            RaftNode* raftNodeOfSenderInRecver = getRaftNodeFromConfig(msg->recver->raft,msg->sender->raft->nodeId);
            recvRequestVote(msg->recver->raft,raftNodeOfSenderInRecver,&(msg->data.rvReq),&(respMsg->data.rvResp));
            //应答消息
            sendMsg(msg->recver,msg->sender,respMsg);
        }
        break;
    case REQUESTVOTE_RESP:
        {
            //从接收者Raft实例中获取发送者对应的RaftNode
            RaftNode* raftNodeOfSenderInRecver = getRaftNodeFromConfig(msg->recver->raft,msg->sender->raft->nodeId);
            recvRequestVoteResponse(msg->recver->raft,raftNodeOfSenderInRecver,&(msg->data.rvResp));
        }
        break;
    case APPENDENTRIES:
        {
            Msg* respMsg = new (std::nothrow) Msg();
            assert(respMsg!=nullptr);
            respMsg->type = APPENDENTRIES_RESP;

            //从接收者Raft实例中获取发送者对应的RaftNode
            RaftNode* raftNodeOfSenderInRecver = getRaftNodeFromConfig(msg->recver->raft,msg->sender->raft->nodeId);
            recvAppendEntriesRequest(msg->recver->raft,raftNodeOfSenderInRecver,&(msg->data.aeReq),&(respMsg->data.aeResp));
            //应答消息
            sendMsg(msg->recver,msg->sender,respMsg);
        }
        break;
    case APPENDENTRIES_RESP:
        {
            //从接收者Raft实例中获取发送者对应的RaftNode
            RaftNode* raftNodeOfSenderInRecver = getRaftNodeFromConfig(msg->recver->raft,msg->sender->raft->nodeId);
            recvAppendEntriesResponse(msg->recver->raft,raftNodeOfSenderInRecver,&(msg->data.aeResp));
        }
        break;
    default:
        break;
    }
    //释放输入的msg
    delete msg;
}

void Cluster::deleteMsg(Msg* msg){
    delete msg;
}

Node::Node():nodeId(static_cast<uint64_t>(-1)),raft(nullptr),connectionStatus(DISCONNECTED){

}

Node::Node(uint64_t id){
    this->nodeId = id;
    this->raft = getNewRaft();
    this->raft->nodeId = id;
    this->connectionStatus = DISCONNECTED;
}

Node::~Node(){
    this->connectionStatus = DISCONNECTED;
    deleteRaft(this->raft);
    this->raft = nullptr;
    this->nodeId = static_cast<uint64_t>(-1);
}

void Node::core(int go){
    assert(this->raft != nullptr);
    addRaftTime(this->raft,go);
    raftCore(this->raft);
}

/**
 * 在Cluster中发送RequestVoteRequest请求。
 * 返回0-成功
 */
int sendRequestVoteRequestInCluster(RaftInterfaceType raftPtr,RaftNodeInterfaceType nodePtr,UserDataInterfaceType raftUserData,const RequestVoteRequest* reqPtr){
    assert(raftPtr!=nullptr);
    assert(nodePtr!=nullptr);
    assert(reqPtr!=nullptr);

    Node* fromNode = static_cast<Node*>(raftPtr->userData);
    assert(fromNode!=nullptr);
    Cluster* cluster = static_cast<Cluster*>(fromNode->cluster);
    assert(cluster!=nullptr);
    uint64_t toNodeId = nodePtr->nodeId;
    Node* toNode = cluster->getNodeByNodeId(toNodeId);

    Msg* msg = new(std::nothrow) Msg();
    msg->type = REQUESTVOTE;
    msg->data.rvReq = *reqPtr;
    
    cluster->sendMsg(fromNode,toNode,msg);

    return 0;
}

/**
 * 在Cluster中发送AppendEntriesRequest请求。
 * 返回0-成功
 */
int sendAppendEntriesRequestInCluster(RaftInterfaceType raftPtr,RaftNodeInterfaceType nodePtr,UserDataInterfaceType raftUserData,const AppendEntriesRequest* reqPtr){
    assert(raftPtr!=nullptr);
    assert(nodePtr!=nullptr);
    assert(reqPtr!=nullptr);

    Node* fromNode = static_cast<Node*>(raftPtr->userData);
    assert(fromNode!=nullptr);
    Cluster* cluster = static_cast<Cluster*>(fromNode->cluster);
    assert(cluster!=nullptr);
    uint64_t toNodeId = nodePtr->nodeId;
    Node* toNode = cluster->getNodeByNodeId(toNodeId);

    Msg* msg = new(std::nothrow) Msg();
    msg->type = APPENDENTRIES;
    msg->data.aeReq = *reqPtr;
    //深度复制消息
    AppendEntriesRequest* aeReqPtr=&(msg->data.aeReq);
    if(reqPtr->entryCount > 0){
        aeReqPtr->entries = nullptr;
        aeReqPtr->entries = new(std::nothrow) Entry[reqPtr->entryCount];
        for(int i=0;i<reqPtr->entryCount;++i){
            aeReqPtr->entries[i] = reqPtr->entries[i];
            if(reqPtr->entries[i].data != nullptr){
                aeReqPtr->entries[i].data = sdsdup(reqPtr->entries[i].data);
            }
        }
    }

    cluster->sendMsg(fromNode,toNode,msg);

    return 0;
}

}