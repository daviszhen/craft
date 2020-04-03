#include <cassert>
#include <new>
#include <algorithm>

#include "simulator.h"

namespace Simulator{

DEFINE_int32(nodeCount,3,"the count of node in the cluster. default 3");
DEFINE_int32(maxAdditionCount,5,"the max addition count of node in the cluster. default 5");
DEFINE_bool(enableRandomPeriod,true,"enable random period for timeGone in Raft. (true/false, default true)");
DEFINE_int32(clientSubmitDataRate,100,"the rate that client submit data into rafe cluster. (0 - 100, default 100)");
DEFINE_int32(msgDropRate,0,"the rate that msg dropped in cluster. (0 - 100, default 0)");
DEFINE_int32(msgDuplicateRate,0,"the rate that msg duplicated in cluster.(0 - 100, default 0)");
DEFINE_int32(msgDelayRate,0,"the rate that msg delayed in cluster.(0 - 100,default 0");
DEFINE_int32(partitionRate,0,"the rate that network partitions emerges or healed. (0 - 100, default 0)");
DEFINE_int32(memberChangeRate,0,"the rate that membership changes in cluster. (0 - 1000),default 0");
DEFINE_bool(enableCheckLiveness,true,"enable cluster liveness check. (true/false, default) true");
DEFINE_string(lmdbPath,"store","the path of directory for the lmdb database. default 'store'");
DEFINE_int32(lmdbMMSize,1024,"the size of the memory map to use for lmdb. default 1024MB");
DEFINE_string(lmdbRaftStates,"raftStates","the name of lmdb database for saving raft states.default 'raftStates'");
DEFINE_string(lmdbRaftEntries,"raftEntries","the name of lmdb database for saving raft entries.default 'raftEntries");
DEFINE_int32(rebootRate,0,"the rate that node rebooting. (0 - 1000),default 0");
DEFINE_int32(minRebootInterval,60,"the min interval between two adjacent rebooting. default 60s");

uint64_t Cluster::nodeId = 0;

Cluster::Cluster():nodes(),deletingNodes(),rebootingNodes(),
        messages(),partitions(),nodeId2Nodes(),rd(),gen(rd()),
        dis100(1,100),dis1000(1, 1000),
        lastAppliedLogIndex(0),lastAppliedLogClock(0),
        livenessCheckThreshold(0),clusterClock(0)    
        {
        
    }

Cluster::~Cluster(){
    for(auto msg: this->messages){
        delete msg;
    }

    for(auto node : this->nodes){
        delete node;
    }

    for(auto node : this->deletingNodes){
        delete node;
    }

    for(auto node : this->rebootingNodes){
        delete node;
    }

    sdsfree(this->raftConfig);
}

bool Cluster::addNode(Node* nodePtr){
    assert(nodePtr!=nullptr);
    if(nodeIsActive(nodePtr) || 
            nodeIsRebooting(nodePtr) || 
            nodeIsDeleting(nodePtr)){
        return false;
    }

    nodes.push_back(nodePtr);
    return true;
}

void Cluster::removeNode(Node* nodePtr){
    assert(nodePtr!=nullptr);
    removeNodeFromActiveQueue(nodePtr);
    removeNodeFromRebootingQueue(nodePtr);
    removeNodeFromDeletingQueue();
}

bool Cluster::nodeIsActive(Node* nodePtr){
    for(int i=0;i < this->nodes.size();i++){
        if(this->nodes[i] == nodePtr){
            return true;
        }
    }
    return false;
}

bool Cluster::nodeIsRebooting(Node* nodePtr){
    for(int i=0;i < this->rebootingNodes.size();i++){
        if(this->rebootingNodes[i] == nodePtr){
            return true;
        }
    }
    return false;
}

bool Cluster::nodeIsDeleting(Node* nodePtr){
    for(int i=0;i < this->deletingNodes.size();i++){
        if(this->deletingNodes[i] == nodePtr){
            return true;
        }
    }
    return false;
}

void Cluster::removeNodeFromActiveQueue(Node* nodePtr){
    std::deque<Node*>::iterator it = 
        std::find(this->nodes.begin(),this->nodes.end(),nodePtr);
    if(it != this->nodes.end()){
        this->deletingNodes.push_back(*it);
        this->nodes.erase(it);
    }
}

void Cluster::removeNodeFromRebootingQueue(Node* nodePtr){
    std::deque<Node*>::iterator it = 
        std::find(this->rebootingNodes.begin(),this->rebootingNodes.end(),nodePtr);
    if(it != this->rebootingNodes.end()){
        this->deletingNodes.push_back(*it);
        this->rebootingNodes.erase(it);
    }
}

void Cluster::removeNodeFromDeletingQueue(){
    for(auto node : this->deletingNodes){
        if(nodeIsActive(node)){
            removeNodeFromActiveQueue(node);
        }
        if(nodeIsRebooting(node)){
            removeNodeFromRebootingQueue(node);
        }
    }
    std::deque<Node*>::iterator it = std::unique(this->deletingNodes.begin(),this->deletingNodes.end());
    this->deletingNodes.erase(it,this->deletingNodes.end());
    while(this->deletingNodes.size()){
        Node* node = this->deletingNodes.front();this->deletingNodes.pop_front();
        delete node;
    }
}

Node* Cluster::getNodeFromActiveQueue(uint64_t nodeId){
    for(auto node : this->nodes){
        if(node->nodeId == nodeId){
            return node;
        }
    }

    return nullptr;
}

Node* Cluster::getNodeFromRebootingQueue(uint64_t nodeId){
    for(auto node : this->rebootingNodes){
        if(node->nodeId == nodeId){
            return node;
        }
    }

    return nullptr;
}

Node* Cluster::getNodeFromDeletingQueue(uint64_t nodeId){
    for(auto node : this->deletingNodes){
        if(node->nodeId == nodeId){
            return node;
        }
    }

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
    //成员变更
    if(dis1000(gen) < FLAGS_memberChangeRate){
        if(50 < dis100(gen)){
            if(this->nodes.size() < FLAGS_nodeCount + 2 * FLAGS_maxAdditionCount ){
                addServerIntoRaft();
            }
        }else{
            //TODO:
            if(this->nodes.size() > 1){
                removeServerFromRaft();
            }
        }
    }

    //节点重启
    if(dis1000(gen) < FLAGS_rebootRate){
        if(dis100(gen) < 100){
            if(this->nodes.size() > (FLAGS_nodeCount / 2 + 1)){
                rebootNode();
            }
        }
    }

    //生成网络分区
    if(dis100(gen) < FLAGS_partitionRate){
        addPartition();
    }
    //删除网络分区
    if(dis100(gen) < FLAGS_partitionRate){
        removePartition();
    }
    //client提交数据给集群
    if(dis100(gen) < FLAGS_clientSubmitDataRate){
        submitDataIntoCluster(dis100(gen),dis100(gen));
    }
    //重启完成的节点再恢复执行
    if(this->rebootingNodes.size()){
        std::deque<Node*> tmp;
        tmp.swap(this->rebootingNodes);
        for(auto node : tmp){
            //时间点未到，不恢复执行
            if(node->readyTime > std::chrono::steady_clock::now()){
                this->rebootingNodes.push_back(node);
            }else{
                if(!nodeIsActive(node)){
                    this->nodes.push_back(node);
                }
            }
        }
    }

    //驱动节点执行
    std::deque<Node*> ex(this->nodes.begin(),this->nodes.end());
    for(auto node : ex){       
        if(!nodeIsActive(node)){
            continue;
        }
        if(FLAGS_enableRandomPeriod){
            node->core(dis100(gen));
        }else{
            node->core(100);
        }
        //清理掉要删除的节点
        if(nodeIsDeleting(node)){
            removeNodeFromDeletingQueue();
        }
    }

    //清理掉要删除的节点
    removeNodeFromDeletingQueue();

    //检测活性
    if(FLAGS_enableCheckLiveness){
        checkLiveness();
    }
}

void Cluster::dispatch(){
    std::deque<Msg*> oldMsgQueue;
    this->messages.swap(oldMsgQueue);

    while(oldMsgQueue.size()){
        Msg* msg = oldMsgQueue.front();
        oldMsgQueue.pop_front();

        //消息delay,已经delay过的消息，不再被投递
        if(msg->readyTime == std::chrono::steady_clock::time_point()){
            //未设置过延迟
            if(dis100(gen) < FLAGS_msgDelayRate){
                msg->readyTime = std::chrono::steady_clock::now();
                //delay一个[0-100]妙的随机时间
                msg->readyTime  += std::chrono::seconds(dis100(gen) / 2);
                this->messages.push_back(msg);
                continue;
            }
        }else if(msg->readyTime > std::chrono::steady_clock::now()){
            //时间未到不投递
            this->messages.push_back(msg);
            continue;
        }
        
        deliver(msg);
        deleteMsg(msg);
        //检测lastLogIndex处是否有Entry
        checkLastLogIndexValidity();
        //检测选举安全性
        checkElectionSafety();
    }
}

uint64_t Cluster::getNextNodeId(){
    return ++nodeId;
}

void Cluster::printNodes(){
    int i=0;
    for(auto node : this->nodes){
        Raft* raftPtr = node->raft;
        const char* roleStr[3]={
            "Follower",
            "\033[33m\033[01mCandidate\033[0m",
            "\033[31m\033[01m\033[05mLeader\033[0m"
        };
        char votedForInfo[1024];
        if(raftPtr->votedFor == ((uint64_t)-1)){
            snprintf(votedForInfo,sizeof(votedForInfo),"-1");
        }else{
            snprintf(votedForInfo,sizeof(votedForInfo),"%lu",raftPtr->votedFor);
        }
        fprintf(stderr,
            "%d : %lu (role %s) rebootCount %d nodeCount %d term %d votedFor %s commitIndex %d lastApplied %d "
            "lastLogIndex %d lastLogTerm %d lastConfigEntryIndex %d lastNoopEntryIndex %d " 
            "timeGone %d election %d heartbeat %d \n",
            i++,
            raftPtr->nodeId,
            roleStr[raftPtr->role],
            node->rebootCount,
            raftPtr->nodeCount,
            raftPtr->currentTerm,
            votedForInfo,
            raftPtr->commitIndex,
            raftPtr->lastApplied,
            logLastLogIndex(raftPtr->log),
            logLastLogTerm(raftPtr->log),
            getLastConfigEntryIndex(raftPtr->log),
            getLastNoopEntryIndex(raftPtr->log),
            getRaftTime(raftPtr),raftPtr->electionTimeout,raftPtr->heartbeatTimeout);
    }
    fprintf(stderr,"\n");
}

void Cluster::printRebootingNodes(){
int i=0;
    for(auto node : this->rebootingNodes){
        Raft* raftPtr = node->raft;
        const char* roleStr[3]={
            "Follower",
            "\033[33m\033[01mCandidate\033[0m",
            "\033[31m\033[01m\033[05mLeader\033[0m"
        };
        char votedForInfo[1024];
        if(raftPtr->votedFor == ((uint64_t)-1)){
            snprintf(votedForInfo,sizeof(votedForInfo),"-1");
        }else{
            snprintf(votedForInfo,sizeof(votedForInfo),"%lu",raftPtr->votedFor);
        }

        int timeleft = 0;
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if(node->readyTime > now){
            timeleft = std::chrono::duration_cast<std::chrono::seconds>(node->readyTime - now).count();
        }else{
            timeleft = 0;
        }
        fprintf(stderr,
            "%d : %lu (role %s) rebooting timeleft %d s rebootCount %d nodeCount %d term %d votedFor %s commitIndex %d lastApplied %d "
            "lastLogIndex %d lastLogTerm %d lastConfigEntryIndex %d lastNoopEntryIndex %d " 
            "timeGone %d election %d heartbeat %d \n",
            i++,
            raftPtr->nodeId,
            roleStr[raftPtr->role],
            timeleft,
            node->rebootCount,
            raftPtr->nodeCount,
            raftPtr->currentTerm,
            votedForInfo,
            raftPtr->commitIndex,
            raftPtr->lastApplied,
            logLastLogIndex(raftPtr->log),
            logLastLogTerm(raftPtr->log),
            getLastConfigEntryIndex(raftPtr->log),
            getLastNoopEntryIndex(raftPtr->log),
            getRaftTime(raftPtr),raftPtr->electionTimeout,raftPtr->heartbeatTimeout);
    }
    fprintf(stderr,"\n");
}

void Cluster::sendMsg(Node* sender,Node* recver,Msg* msg){
    //assert(sender != nullptr && sender->raft != nullptr);
    //assert(recver != nullptr && recver->raft != nullptr);
    msg->sender = sender;
    msg->recver = recver;

    //如果<sender,recver>在分区中就丢掉此消息
    if(this->partitions.count(std::make_pair(sender,recver))){
        deleteMsg(msg);
        return;
    }

    //丢弃消息
    if(dis100(gen) < FLAGS_msgDropRate){
        deleteMsg(msg);
        return;
    }
    //重复消息
    while(dis100(gen) < FLAGS_msgDuplicateRate){
        //复制消息
        Msg* dupMsg = new (std::nothrow) Msg(*msg);
        this->messages.push_back(dupMsg);
    }

    this->messages.push_back(msg);
}

void Cluster::addPartition(){
    if(this->nodes.size()<=1){
        return;
    }
    std::vector<Node*> temp(this->nodes.begin(),this->nodes.end());
    std::random_shuffle(temp.begin(),temp.end());
    Node* node1 = temp.back();temp.pop_back();
    Node* node2 = temp.back();temp.pop_back();
    this->partitions.insert(std::make_pair(node1,node2));
}

void Cluster::removePartition(){
    if(this->partitions.size() < 1){
        return ;
    }
    std::vector<std::pair<Node*,Node*>> temp(this->partitions.begin(),this->partitions.end());
    std::random_shuffle(temp.begin(),temp.end());
    std::pair<Node*,Node*> part = temp.back();
    this->partitions.erase(part);
}

void Cluster::addServerIntoRaft(){
    for(auto node : this->nodes){
        if(isLeader(node->raft)){
            uint64_t newId = getNextNodeId();
            sds data= sdsempty();
            data = sdscatfmt(data,"%U",newId);
            int index=-1,term=-1;
            submitDataToRaft(node->raft,data,sdslen(data),ADD_NODE,&index,&term);
            fprintf(stderr,"server %lu add new server %lu index %d term %d \n",node->nodeId,newId,index,term);
            sdsfree(data);
            break;
        }
    }
}

void Cluster::removeServerFromRaft(){
    if(this->nodes.size()<=1){
        return;
    }
    std::vector<Node*> temp(this->nodes.begin(),this->nodes.end());
    std::random_shuffle(temp.begin(),temp.end());
    //随机选一个Raft
    Node* oldNode = temp.front();
    assert(oldNode != nullptr);
    for(auto node : this->nodes){
        if(isLeader(node->raft)){
            uint64_t oldId = oldNode->nodeId;
            sds data= sdsempty();
            data = sdscatfmt(data,"%U",oldId);
            int index=-1,term=-1;
            submitDataToRaft(node->raft,data,sdslen(data),DISABLE_VOTE,&index,&term);
            sdsfree(data);
            fprintf(stderr,"server %lu disable old server %lu index %d term %d \n",node->nodeId,oldId,index,term);
            break;
        }
    }
}

void Cluster::rebootNode(){
    if(this->nodes.empty()){
        return;
    }
    std::vector<Node*> temp(this->nodes.begin(),this->nodes.end());
    std::random_shuffle(temp.begin(),temp.end());
    //随机选一个Raft
    Node* node = temp.back();temp.pop_back();
    std::deque<Node*>::iterator it = 
        std::find(this->nodes.begin(),this->nodes.end(),node);
    assert(it != this->nodes.end());

    //检查重启间隔
    if(node->readyTime + std::chrono::seconds(FLAGS_minRebootInterval * 2) < std::chrono::steady_clock::now()){
        //从执行队列删除，再加入重启队列
        this->nodes.erase(it);
        this->rebootingNodes.push_back(node);

        node->rebootRaft();
        //计算reboot后，再进入调度队列的时间点
        node->readyTime = std::chrono::steady_clock::now();
        //delay一个[0-100]妙的随机时间
        node->readyTime  += std::chrono::seconds(dis100(gen) / 2);
    }
}

void Cluster::deliver(Msg* msg){
    assert(msg != nullptr);
    if(!nodeIsActive(msg->recver) || !nodeIsActive(msg->sender)){
        //接收节点不在集群中，不投递，返回
        return ;
    }
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
    case ADDREMOVESERVER:
        {
            
        }
        break;
    case ADDREMOVESERVER_RESP:
        {

        }
        break;
    default:
        break;
    }
}

void Cluster::deleteMsg(Msg* msg){
    delete msg;
}

void Cluster::checkElectionSafety(){
    int leaderCount = 0;
    int leaderIndex = -1;
    int leaderTerm = -1;
    for(int i=0;i<this->nodes.size();++i){
        if(!isLeader(this->nodes[i]->raft)){
            continue;
        }
        if(leaderIndex == -1){
            leaderIndex = i;
            leaderTerm = this->nodes[i]->raft->currentTerm;
            ++leaderCount;
        }else if(this->nodes[i]->raft->currentTerm ==  leaderTerm){
            ++leaderCount;
        }
        assert(leaderCount <= 1);
        for(int j=i+1;j<this->nodes.size();++j){
            int term1 = this->nodes[i]->raft->currentTerm;
            //int index1 = logLastLogIndex(this->nodes[i]->raft->log);
            int term2 = this->nodes[j]->raft->currentTerm;
            //int index2 = logLastLogIndex(this->nodes[j]->raft->log);
            if(isLeader(this->nodes[j]->raft) && term1 == term2 /*&& index1 == index2*/){
                fprintf(stderr,"Election Safety failed. node:[%lu %lu] in same term %d \n",
                    this->nodes[i]->nodeId,this->nodes[j]->nodeId,term1);
                assert(0);
            }
        }
    }
    
}

void Cluster::checkLogMatching(const RaftInterfaceType raftPtr,const Entry* e){
    assert(raftPtr!=nullptr);
    assert(e!=nullptr);

    for(auto node : this->nodes){
        if(node->raft->nodeId == raftPtr->nodeId){
            continue;
        }
        int otherCommitIndex = node->raft->commitIndex;
        int myCommitIndex = raftPtr->commitIndex;
        //此raft实例的commitIndex和此Entry的index 都<= 其它raft实例的commitIndex时，
        //才能比较
        if(myCommitIndex <= otherCommitIndex && e->index <= otherCommitIndex){
            Entry ent = logAt(node->raft->log,e->index);
            assert(ent.type == e->type);
            assert(ent.index == e->index);
            assert(ent.term == e->term);
            assert(ent.dataLen == e->dataLen);
            assert(sdscmp(ent.data,e->data) == 0);
        }
    }
}

void Cluster::checkLastLogIndexValidity(){
    for(auto node : this->nodes){
        Raft* raftPtr = node->raft;
        const int index = logLastLogIndex(raftPtr->log);
        assert(index >= 0 && index == logAt(raftPtr->log,index).index);
    }
}

void Cluster::checkEntryIndexMonotonicity(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr != nullptr);
    assert(entPtr != nullptr);
    const int index = logLastLogIndex(raftPtr->log);
    assert(index >= 0);
    assert(index < entPtr->index);
    assert(logLastLogTerm(raftPtr->log) <= entPtr->term);
}

void Cluster::checkCommittedEntriesDeleted(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr != nullptr);
    assert(entPtr != nullptr);
    assert(indexIsCommitted(raftPtr,entPtr->index) == 0);
}

void Cluster::checkCommittedEntriesDeletedAtBegin(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr != nullptr);
    assert(entPtr != nullptr);
    assert(indexIsCommitted(raftPtr,entPtr->index) != 0);
}

void Cluster::checkLiveness(){
    if(lastAppliedLogIndex != 0 && lastAppliedLogClock + livenessCheckThreshold < clusterClock){
        bool hasLeader = false;
        for(auto node : this->nodes){
            if(isLeader(node->raft)){
                hasLeader = true;
            }
        }
        if(hasLeader){
            fprintf(stderr,"there is a leader but it still can not work\n");
        }else{
            fprintf(stderr,"there is no leader\n");
        }
        assert(0);
    }
}

void Cluster::checkLeaderAppendOnly(const RaftInterfaceType raftPtr){
    assert(raftPtr != nullptr);
    assert(!isLeader(raftPtr));
}

Node::Node():nodeId(static_cast<uint64_t>(-1)),raft(nullptr),connectionStatus(DISCONNECTED),
        raftMdbEnv(nullptr),raftStates(UINT_MAX),raftEntries(UINT_MAX),
        readyTime(),rebootCount(0){
}

Node::Node(uint64_t id):readyTime(),rebootCount(0){
    this->nodeId = id;
    this->raft = getNewRaft();
    this->raft->nodeId = id;
    this->connectionStatus = DISCONNECTED;
    //每个节点都创建不同的lmdb数据库路径
    lmdbPath = FLAGS_lmdbPath + std::to_string(id);
    Lmdb::createLmdbEnv(&raftMdbEnv,0,lmdbPath.c_str(),FLAGS_lmdbMMSize);
    //states数据库的key是字典序
    Lmdb::createLmdbDb(raftMdbEnv,&raftStates,FLAGS_lmdbRaftStates.c_str(),0);
    //entries数据库的key是数字序
    Lmdb::createLmdbDb(raftMdbEnv,&raftEntries,FLAGS_lmdbRaftEntries.c_str(),MDB_INTEGERKEY);
}

Node::~Node(){
    this->connectionStatus = DISCONNECTED;
    deleteRaft(this->raft);
    this->raft = nullptr;
    this->nodeId = static_cast<uint64_t>(-1);
    while(this->machineLog.size()){
        Entry e=this->machineLog.front();this->machineLog.pop_front();
        deleteEntry(&e);
    }
    MDB_dbi dbs[]={raftStates,raftEntries};
    Lmdb::lmdbDeleteDb(raftMdbEnv,dbs,sizeof(dbs)/sizeof(MDB_dbi));
    raftStates = UINT_MAX;
    raftEntries = UINT_MAX;
    Lmdb::lmdbDeleteEnv(raftMdbEnv,lmdbPath.c_str());
    raftMdbEnv = NULL;
    lmdbPath += "-deleted";
}

void Node::core(int go){
    assert(this->raft != nullptr);
    addRaftTime(this->raft,go);
    raftCore(this->raft);
}

void Node::persistRaftStates(const RaftInterfaceType raftPtr){
    assert(raftPtr!=nullptr);
    Lmdb::lmdbPutStringInt(raftMdbEnv,raftStates,const_cast<char*>("currentTerm"),raftPtr->currentTerm);
    Lmdb::lmdbPutStringUint64(raftMdbEnv,raftStates,const_cast<char*>("votedFor"),raftPtr->votedFor);
    Lmdb::lmdbPutStringInt(raftMdbEnv,raftStates,const_cast<char*>("commitIndex"),raftPtr->commitIndex);
    //TODO:save lastConfiguration
}

void Node::addEntryAtEnd(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    const int dataLen = sizeof(Entry) + entPtr->dataLen;
    char* data=new char[dataLen];
    Entry* e=(Entry*)data;
    *e = *entPtr;
    e->data = nullptr;
    //将Entry数据复制到缓存区尾部
    memcpy(data+sizeof(Entry),entPtr->data,entPtr->dataLen);
    MDB_val key,value;
    key.mv_data = &e->index;
    key.mv_size = sizeof(e->index);
    value.mv_data = data;
    value.mv_size = dataLen;
    Lmdb::lmdbPut(raftMdbEnv,raftEntries,key,value);
    delete [] data;
    #if 0
    fprintf(stderr,"%lu addEntry term %d index %d type %d dataLen %d data %s\n",
        raftPtr->nodeId,
        entPtr->term,entPtr->index,entPtr->type,entPtr->dataLen,entPtr->data);
    #endif
}

void Node::deleteEntryAtEnd(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    MDB_val key,value;
    Lmdb::lmdbDeleteAtEnd(raftMdbEnv,raftEntries,&key,&value);
    int kIndex = *static_cast<int*>(key.mv_data);
    Entry* e=static_cast<Entry*>(value.mv_data);
    #if 0
    fprintf(stderr,"%lu deleteEntryAtEnd {term %d index %d type %d dataLen %d data %s} - {term %d index %d type %d dataLen %d data %s}\n",
        raftPtr->nodeId,e->term,e->index,e->type,e->dataLen,e->data,
        entPtr->term,entPtr->index,entPtr->type,entPtr->dataLen,entPtr->data);
    #endif
}

void Node::deleteEntryAtBegin(const RaftInterfaceType raftPtr,const Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    MDB_val key,value;
    Lmdb::lmdbDeleteAtBegin(raftMdbEnv,raftEntries,&key,&value);
}

void Node::loadLogEntries(RaftInterfaceType raftPtr){
    assert(raftPtr!=nullptr);
    MDB_cursor* curs = nullptr;
    MDB_txn *txn = nullptr;
    MDB_val k,v;
    int ret = mdb_txn_begin(raftMdbEnv,NULL,MDB_RDONLY,&txn);
    lmdbAssertIsZero(ret);

    ret = mdb_cursor_open(txn,raftEntries,&curs);
    lmdbAssertIsZero(ret);

    int count = 0;

    //逐条提取Entry并添加到Raft实例中
    ret = mdb_cursor_get(curs,&k,&v,MDB_FIRST);
    if(ret == MDB_NOTFOUND){
        mdb_cursor_close(curs);

        ret = mdb_txn_commit(txn);
        lmdbAssertIsZero(ret);
        return;
    }else if(ret != 0){
        lmdbAssertIsZero(ret);
    }

    do
    {
        int kIndex;
        assert(sizeof(int) == k.mv_size);
        kIndex = *static_cast<int*>(k.mv_data);
        Entry ent=*static_cast<Entry*>(v.mv_data);
        ent.data = sdsempty();
        ent.data = sdscatlen(ent.data,static_cast<char*>(v.mv_data)+sizeof(Entry),ent.dataLen);
        int nindex = logAppend(raftPtr,ent);
        assert(kIndex == ent.index);
        assert(nindex == ent.index);
        sdsfree(ent.data);

        ret = mdb_cursor_get(curs,&k,&v,MDB_NEXT);

        ++count;
    } while (ret == 0);
    
    mdb_cursor_close(curs);

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    //提取commitIndex并设置
    int commitIndex=0;
    Lmdb::lmdbGetStringInt(raftMdbEnv,raftStates,const_cast<char*>("commitIndex"),&commitIndex);
    setRaftCommitIndex(raftPtr,commitIndex);

    //应用日志
    applyLog(raftPtr,commitIndex+1);
}

void Node::loadStates(RaftInterfaceType raftPtr){
    assert(raftPtr!=nullptr);
    int currentTerm = 0;
    Lmdb::lmdbGetStringInt(raftMdbEnv,raftStates,const_cast<char*>("currentTerm"),&currentTerm);
    uint64_t votedFor = static_cast<uint64_t>(-1);
    Lmdb::lmdbGetStringUint64(raftMdbEnv,raftStates,const_cast<char*>("votedFor"),&votedFor);
    int commitIndex=0;
    Lmdb::lmdbGetStringInt(raftMdbEnv,raftStates,const_cast<char*>("commitIndex"),&commitIndex);
    setRaftCurrentTerm(raftPtr,currentTerm);
    setRaftVotedFor(raftPtr,votedFor);
    setRaftCommitIndex(raftPtr,commitIndex);
}

void Node::rebootRaft(){
    //释放Raft实例
    this->connectionStatus = DISCONNECTED;
    deleteRaft(this->raft);
    this->raft = nullptr;
    while(this->machineLog.size()){
        Entry e=this->machineLog.front();this->machineLog.pop_front();
        deleteEntry(&e);
    }
    //重新生成Raft实例
    this->raft = getNewRaft();
    this->raft->nodeId = this->nodeId;
    this->connectionStatus = DISCONNECTED;

    setRaftInterface(this->raft,&cluster->raftInterfaces,this);
    //TOFIX:先关闭AddEntry钩子函数
    this->raft->interfaces.addingEntry = NULL;
    addRaftNodeIntoConfig(this->raft,this->nodeId,this,1);
    Cluster* cluster = this->cluster;
    assert(cluster != nullptr);
    
    //无需单独加载初始配置，保存的日志中有配置的。
    //加载初始配置
    //submitDataToRaft(this->raft,cluster->raftConfig,strlen(cluster->raftConfig),LOAD_CONFIG,NULL,NULL);
    //加载状态
    loadStates(this->raft);

    //加载日志
    loadLogEntries(this->raft);

    //再打开AddEntry钩子函数
    this->raft->interfaces.addingEntry = cluster->raftInterfaces.addingEntry;
    rebootCount++;
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
    Node* toNode = cluster->getNodeFromActiveQueue(toNodeId);

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
    Node* toNode = cluster->getNodeFromActiveQueue(toNodeId);

    Msg* msg = new(std::nothrow) Msg();
    msg->type = APPENDENTRIES;
    msg->data.aeReq = *reqPtr;
    //深度复制消息
    AppendEntriesRequest* aeReqPtr=&(msg->data.aeReq);
    if(reqPtr->entryCount > 0){
        aeReqPtr->entries = nullptr;
        aeReqPtr->entries = new(std::nothrow) Entry[reqPtr->entryCount];
        for(int i=0;i<reqPtr->entryCount;++i){
            copyEntryTo(reqPtr->entries+i,aeReqPtr->entries+i);
        }
    }

    cluster->sendMsg(fromNode,toNode,msg);

    return 0;
}

int addingEntryInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftUserData,Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    node->cluster->checkEntryIndexMonotonicity(raftPtr,entPtr);
    node->addEntryAtEnd(raftPtr,entPtr);
    return 0;
}

int deletingEntryAtEndInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftUserData,Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    node->cluster->checkCommittedEntriesDeleted(raftPtr,entPtr);
    node->cluster->checkLeaderAppendOnly(raftPtr);
    
    node->deleteEntryAtEnd(raftPtr,entPtr);

    sdsfree(entPtr->data);
    entPtr->data = nullptr;
    entPtr->dataLen = 0;
    return 0;
}

int deletingEntryAtBeginInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftUserData,Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    node->cluster->checkCommittedEntriesDeletedAtBegin(raftPtr,entPtr);

    node->deleteEntryAtBegin(raftPtr,entPtr);

    sdsfree(entPtr->data);
    entPtr->data = nullptr;
    entPtr->dataLen = 0;
    return 0;
}

/**
 * Raft实例应用Entry到状态机函数接口
 * 
 */
int applyLogInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftUserData,Entry* entPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    //检测日志匹配和状态机安全特性
    node->cluster->checkLogMatching(raftPtr,entPtr);
    //更新lastAppliedIndex
    if(entPtr->index > node->cluster->getLastAppliedLogIndex()){
        node->cluster->setLastAppliedLogIndex(entPtr->index);
        node->cluster->setLastAppliedLogClock(node->cluster->getClusterClock());
    }
    //node->putEntryIntoMachine(entPtr);
    //配置变更测试
    switch (entPtr->type)
    {
    case ADD_NODE:
        {
            if(isLeader(raftPtr)){
                uint64_t nodeId = strtoull(entPtr->data,nullptr,10);
                int index=-1,term=-1;
                submitDataToRaft(node->raft,entPtr->data,sdslen(entPtr->data),ENABLE_VOTE,&index,&term);
                //fprintf(stderr,"server %lu enable vote of server %lu index %d term %d \n",
                //    node->nodeId,nodeId,index,term);
            }
        }
        break;
    case ENABLE_VOTE:
        {
            uint64_t nodeId = strtoull(entPtr->data,nullptr,10);
            //fprintf(stderr,"server %lu notify server %lu join the cluster \n",node->nodeId,nodeId);
        }
        break;
    case DISABLE_VOTE:
        {
            if(isLeader(raftPtr)){
                uint64_t nodeId = strtoull(entPtr->data,nullptr,10);
                int index=-1,term=-1;
                submitDataToRaft(node->raft,entPtr->data,sdslen(entPtr->data),REMOVE_NODE,&index,&term);
                //fprintf(stderr,"server %lu remove server %lu index %d term %d \n",
                //    node->nodeId,nodeId,index,term);
            }
        }
        break;
    case REMOVE_NODE:
        {
            uint64_t nodeId = strtoull(entPtr->data,nullptr,10);
            //fprintf(stderr,"server %lu notify server %lu leave the cluster \n",node->nodeId,nodeId);
        }
        break;
    default:
        break;
    }
    #if 0
    if(entPtr->type == NO_OP){
        if(entPtr->data == NULL){
            raftLog(RAFT_DEBUG,"raft %lu apply no_op index %d term %d cmd is null",raftPtr->nodeId,entPtr->index,entPtr->term);
        }else{
            raftLog(RAFT_DEBUG,"raft %lu apply no_op index %d term %d cmd %s",raftPtr->nodeId,entPtr->index,entPtr->term,entPtr->data);
        }
    }else if(entPtr->type == LOAD_CONFIG){
        raftLog(RAFT_DEBUG,"raft %lu apply LOAD_CONFIG index %d term %d cmd %s",raftPtr->nodeId,entPtr->index,entPtr->term,entPtr->data);
    }else if(entPtr->type == DATA){
        raftLog(RAFT_DEBUG,"raft %lu apply data index %d term %d cmd %s ",raftPtr->nodeId,entPtr->index,entPtr->term,entPtr->data);
    }else{
        raftLog(RAFT_DEBUG,"raft %lu apply data index %d term %d",raftPtr->nodeId,entPtr->index,entPtr->term);
    }
    #endif
    return 0;
}

int getConfigEntryNodeIdInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftUserData,Entry* entPtr,UserDataInterfaceType nodeIdPtr){
    assert(raftPtr!=nullptr);
    assert(entPtr!=nullptr);
    assert(nodeIdPtr!=nullptr);

    //assert(isVoteRightConfigChangeEntry(entPtr->type));
    
    uint64_t nodeId = strtoull(entPtr->data,nullptr,10);
    uint64_t * p = static_cast<uint64_t*>(nodeIdPtr);
    *p = nodeId;
    return 0;
}

int membershipChangeEventInCluster(RaftInterfaceType raftPtr,UserDataInterfaceType raftNodePtr,Entry*,UserDataInterfaceType add){
    assert(raftPtr!=nullptr);
    assert(raftNodePtr!=nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    RaftNode * rfNode = static_cast<RaftNode*>(raftNodePtr);
    uint64_t addOrNot = (uint64_t)add;
    if(addOrNot){
        //向集群中增加新Node
        if(node->cluster->getNodeFromActiveQueue(rfNode->nodeId) == nullptr &&
                node->cluster->getNodeFromRebootingQueue(rfNode->nodeId) == nullptr &&
                node->cluster->getNodeFromDeletingQueue(rfNode->nodeId) == nullptr){
            Node* newNode = new Node(rfNode->nodeId);
            newNode->setNodeCluster(node->cluster);
            //TODO:要不要将自己加入?
            //addRaftNodeIntoConfig(newNode->raft,newNode->nodeId,newNode,0);
            setRaftInterface(newNode->raft,&node->cluster->raftInterfaces,newNode);
            assert(node->cluster->addNode(newNode));
        }
    }else{
        //从集群删除Node
        Node* nodeInCluster =node->cluster->getNodeFromActiveQueue(rfNode->nodeId);
        if(nodeInCluster != nullptr){
            node->cluster->removeNodeFromActiveQueue(nodeInCluster);
        }else if(nodeInCluster = node->cluster->getNodeFromRebootingQueue(rfNode->nodeId)){
            node->cluster->removeNodeFromRebootingQueue(nodeInCluster);
        }
        //TOFIX:会造成自己删除自己或被删除的有要马上执行的惨剧
        if(!node->cluster->nodeIsDeleting(node)){
            node->cluster->removeNodeFromDeletingQueue();
        }
    }

    return 0;
}

void onConversionToLeaderBeforeInCluster(RaftInterfaceType raftPtr){
    assert(raftPtr != nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    node->cluster->checkElectionSafety();
}

void onConversionToLeaderAfterInCluster(RaftInterfaceType raftPtr){
    assert(raftPtr != nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node->cluster != nullptr);
    node->cluster->checkElectionSafety();
}

void persistStateInCluster(const RaftInterfaceType raftPtr){
    assert(raftPtr != nullptr);
    Node* node = static_cast<Node*>(raftPtr->userData);
    assert(node != nullptr);
    node->persistRaftStates(raftPtr);
}
}