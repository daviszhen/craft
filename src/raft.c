#include "raft.h"

//===================================================
Entry* getNewEntry(int count){
    Entry* p = raftCalloc(count,sizeof(Entry));
    raftAssert(p != NULL);
    return p;
}

void deleteEntry(Entry* p){
    if(p->data != NULL){
        sdsfree(p->data);
        p->data = NULL;
        p->dataLen = 0;
    }
}

int getEntrySpaceLength(const Entry* p){
    raftAssert(p!=NULL);
    return sizeof(Entry) + p->dataLen;
}

void expand(Log* logPtr){
    raftAssert(logPtr!=NULL);
    if(logPtr->size < logPtr->capacity){
        return ;
    }

    logPtr->entries = raftRealloc(logPtr->entries,sizeof(Entry) * logPtr->capacity * 2);
    raftAssert(logPtr->entries !=  NULL);
    logPtr->capacity *= 2;
}

Log* getNewLog(){
    Log* ptr = raftCalloc(1,sizeof(Log));
    raftAssert(ptr!=NULL);
    ptr->capacity = 100;
    ptr->entries = raftCalloc(ptr->capacity,sizeof(Entry));
    raftAssert(ptr->entries!=NULL);
    ptr->front = 0;
    ptr->back = 0;
    ptr->lastIncludedIndex = -1;
    ptr->lastIncludedTerm = -1;
    ptr->size = 1;
    ptr->entries[0].data = sdsempty();
    ptr->entries[0].dataLen = 0;
    ptr->entries[0].index = 0;
    ptr->entries[0].term = 0;
    ptr->entries[0].type = NO_OP;
    ptr->lastConfigEntryIndex = -1;
    ptr->lastNoopEntryIndex = 0;
    return ptr;
}

void deleteLog(Log* logPtr){
    raftAssert(logPtr != NULL);
    //释放有的数据
    for(int i=0;i<logPtr->capacity;i++){
        if(logPtr->entries[i].data != NULL){
            sdsfree(logPtr->entries[i].data);
        }
    }
    raftFree(logPtr->entries);
    raftFree(logPtr);
}

int logLength(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastIncludedIndex + 1 + logPtr->size;
}

int logLastIncludedIndex(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastIncludedIndex;
}

int logLastIncludedTerm(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastIncludedTerm;
}

int logFirstLogIndex(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastIncludedIndex + 1;
}

int logLastLogIndex(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logLength(logPtr) - 1;
}

int logLastLogTerm(const Log* logPtr){
    raftAssert(logPtr != NULL);
    return logAt(logPtr,logLastLogIndex(logPtr)).term;
}

Entry logAt(const Log* logPtr,int i){
    raftAssert(logPtr != NULL);
    if(i < logPtr->lastIncludedIndex){
        raftPanic("wrong index i:%d lastIncludedIndex+1:%d",i,logPtr->lastIncludedIndex+1);
    }else if(i == logPtr->lastIncludedIndex){
        Entry t={.data=NULL,.dataLen=0,.type=DATA,.term=logPtr->lastIncludedTerm,.index=logPtr->lastIncludedIndex};
        return t;
    }
    return logPtr->entries[i - logPtr->lastIncludedIndex - 1];
}

int logAppend(Raft* raftPtr,Entry e){
    raftAssert(raftPtr!= NULL);
    Log* logPtr = raftPtr->log;
    raftAssert(logPtr != NULL);
    raftAssert(e.data != NULL);
    expand(logPtr);
    logPtr->entries[logPtr->size++] = e;
    logPtr->back = logPtr->size - 1;
    if(e.type == NO_OP){
        logPtr->lastNoopEntryIndex = logPtr->size - 1;
    }else if(e.type == CONFIG){
        logPtr->lastConfigEntryIndex = logPtr->size - 1;
        loadConfigFromString(raftPtr,e.data,e.dataLen);
    }
    return logPtr->size - 1;
}

int logAppendData(Raft* raftPtr,sds data,int dataLen,enum EntryType type){
    raftAssert(raftPtr != NULL);
    if(data == NULL && dataLen != 0){
        raftLog(RAFT_WARNING,"data is null. but datalen is not null");
        return -1;
    }
    Entry e;
    e.data = sdsempty();
    e.data = sdscatlen(e.data,data,dataLen);
    e.dataLen = dataLen;
    e.term = raftPtr->currentTerm;
    e.type = type;
    e.index = logLength(raftPtr->log);
    return logAppend(raftPtr,e);
}

int logApppendNoop(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    char* noop= "noop";
    return logAppendData(raftPtr,noop,strlen(noop),NO_OP);
}

int logCountFromBegin(const Log* logPtr,int i){
    raftAssert(logPtr != NULL);
    if(i < (logPtr->lastIncludedIndex+1)){
        raftPanic("wrong index i:%d lastIncludedIndex+1:%d 2",i,logPtr->lastIncludedIndex+1);
    }
    return i - logPtr->lastIncludedIndex;
}

void logDeleteAtEnd(Log* logPtr,int N){
    raftAssert(logPtr != NULL);
    if(N > logPtr->size){
        raftLog(RAFT_WARNING,"the count of entries is not enough");
    }
    int count = min(logPtr->size,N);
    logPtr->size -= count;
    logPtr->back = logPtr->size - 1;
    
}

void logDeleteAtBegin(Log* logPtr,int N){
    raftAssert(logPtr != NULL);
    if(N > logPtr->size){
        raftLog(RAFT_WARNING,"the count of entries is not enough 2");
    }
    int count = min(logPtr->size,N);
    //entry前移动
    if(count == logPtr->size){
        logPtr->size = 0;
        logPtr->back = logPtr->size - 1;
        
    }else{
        for(int i=0,j=count;j<logPtr->size;++i,++j){
            logPtr->entries[i] = logPtr->entries[j];
        }
        logPtr->size -= count;
        logPtr->back -= logPtr->size - 1;
    }
}

int getLastConfigEntryIndex(Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastConfigEntryIndex;
}

int getLastNoopEntryIndex(Log* logPtr){
    raftAssert(logPtr != NULL);
    return logPtr->lastNoopEntryIndex;
}

/*
int isServerInConfig(Raft* raftPtr,const char* ip,int port){
    raftAssert(raftPtr!=NULL);
    raftAssert(ip != NULL);
    for(int i=0;i<raftPtr->nodeCount;++i){
        if(strcmp(ip,raftPtr->nodeList[i]->ip) == 0 && port == raftPtr->nodeList[i]->port){
            return 1;
        }
    }
    return 0;
}
*/

int isNodeInConfig(Raft* raftPtr,uint64_t nodeId){
    raftAssert(raftPtr!=NULL);
    for(int i=0;i<raftPtr->nodeCount;++i){
        if(raftPtr->nodeList[i]->nodeId == nodeId){
            return 1;
        }
    }
    return 0;
}

sds makeConfigStringWith(Raft* raftPtr,uint64_t nodeId,UserDataInterfaceType userData){
    raftAssert(raftPtr!=NULL); 
    raftAssert(userData!=NULL);
    sds str = sdsempty();
    for(int i=0;i<raftPtr->nodeCount;++i){
        sds nodeStr = sdsempty();
        getRaftNodeConfigString(raftPtr->nodeList[i],&nodeStr);
        str = sdscatfmt(str,"%s 1 ",nodeStr);
        sdsfree(nodeStr);
    }
    //加上参数中的机器
    str = sdscatfmt(str,"%U ",nodeId);
    if(raftPtr->interfaces.raftNodeUserDataToString){
        sds userDataString = sdsempty();
        raftPtr->interfaces.raftNodeUserDataToString(userData,&userDataString);
        str = sdscat(str,userDataString);
        sdsfree(userDataString);
    }
    str = sdscat(str,"1 ");
    return str;
}

sds makeConfigStringWithout(Raft* raftPtr,uint64_t nodeId/*,UserDataInterfaceType userData*/){
    raftAssert(raftPtr!=NULL); 
    sds str = sdsempty();
    for(int i=0;i<raftPtr->nodeCount;++i){
        sds nodeStr = sdsempty();
        getRaftNodeConfigString(raftPtr->nodeList[i],&nodeStr);
        if(raftPtr->nodeList[i]->nodeId == nodeId){
            //要删除的机器
            str = sdscatfmt(str,"%s 0 ",nodeStr);
        }else{
            str = sdscatfmt(str,"%s 1 ",nodeStr);
        }
        sdsfree(nodeStr);
    }

    return str;
}

sds makeConfigString(Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    sds str = sdsempty();
    for(int i=0;i<raftPtr->nodeCount;++i){
        sds nodeStr = sdsempty();
        getRaftNodeConfigString(raftPtr->nodeList[i],&nodeStr);
        str = sdscatfmt(str,"%s 1 ",nodeStr);
        sdsfree(nodeStr);
    }
    return str;
}

void loadConfigFromString(Raft* raftPtr,const sds str,int len){
    raftAssert(raftPtr!=NULL);
    raftAssert(str!= NULL);
    raftLog(RAFT_DEBUG,"NewConfig %s",str);
    //用空格拆分配置字符串
    int count;
    sds* lines = sdssplitlen(str,len," ",1,&count);
    /*
     * 算法要求一次只能增加或删除一个算法，但在此处无法进行判断。
     * 只能在leader进行处理。
     */
    //一定是nodeId [userdata] 1/0成对出现。由于生成格式最后会多出一个空格，count值
    //可能会是基数但是这不影响解析结果。
    int i=0;
    while(i+1 < count){
        uint64_t nodeId = strtoull(lines[i],NULL,10);
        UserDataInterfaceType userData = NULL;
        int userDataFieldCount = 0;
        if(raftPtr->interfaces.raftNodeStringToUserData){
            userDataFieldCount = raftPtr->interfaces.raftNodeStringToUserData(lines+i+1,count-i-1,&userData);
        }
        const int offset = 2 + userDataFieldCount;
        int need = atoi(lines[i+1+userDataFieldCount]);
        if(need){
            //机器已经在配置中
            if(isNodeInConfig(raftPtr,nodeId)){
                i += offset;
                continue;
            }
            RaftNode * node = getRaftNodeFromConfig(raftPtr,nodeId);
            if(NULL != node){
                //已经有此节点了
                i += offset;
                continue;
            }
            RaftNode* newNode = addRaftNodeIntoConfig(raftPtr,nodeId,userData,0);
            raftAssert(newNode!=NULL);

            //leader要对新增的node的index，进行复位
            if(raftPtr->role == Leader){
                //TODO:快照会不同
                resetRaftNodeIndex(newNode,1,0);
            }
        }else{
            //可以删除此raft所在的机器
            removeRaftNodeFromConfig(raftPtr,nodeId);
        }
        i += offset;
    }

    sdsfreesplitres(lines,count);
}

void getRaftNodeConfigString(RaftNode* nodePtr,sds* str){
    raftAssert(nodePtr!=NULL);
    raftAssert(str!=NULL);
    *str = sdsempty();
    *str = sdscatfmt(*str,"%U ",nodePtr->nodeId);
    if(nodePtr->interfaces.raftNodeUserDataToString){
        sds userDataString = sdsempty();
        nodePtr->interfaces.raftNodeUserDataToString(nodePtr->userData,&userDataString);
        *str = sdscat(*str,userDataString);
        sdsfree(userDataString);
    }
}

//=================================Raft实例相关==================
Raft* getNewRaft(){
    Raft* ptr=raftCalloc(1,sizeof(Raft));
    raftAssert(ptr!=NULL);
    ptr->currentTerm = 0;
    ptr->votedFor = (uint64_t)-1;
    ptr->log = getNewLog();
    ptr->commitIndex = 0;
    ptr->lastApplied = 0;
    ptr->role = Follower;
    ptr->voteMeCount = 0;
    
    //TODO:要细调节
    //
    const int basetime = 100;
    ptr->electionTimeoutBegin = 60 * basetime;
    ptr->electionTimeoutEnd = 75 * basetime;
    ptr->electionTimeout = ptr->electionTimeoutBegin + (int)((ptr->electionTimeoutEnd - ptr->electionTimeoutBegin) * ((float)rand()/(float)RAND_MAX));
    ptr->heartbeatTimeout = 15 * basetime;

    ptr->maxEntryCountInOneAppendEntries = 5;
    ptr->maxAppendEntriesSendInterval = ptr->heartbeatTimeout / 5;

    ptr->timeGone = 0;

    ptr->nodeList = NULL;
    ptr->nodeCount = 0;
    ptr->leaderNode = NULL;
    ptr->selfNode = NULL;

    ptr->countOfLeaderCommittedEntriesInCurrentTerm = 0;
    ptr->maxCountOfApplyingEntry = 5;
    ptr->pendingAddRemoveServerRequest = NULL;
    ptr->pendingAddRemoveServerPeer = NULL;
    return ptr;
}

void deleteRaft(Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    deleteLog(raftPtr->log);
    for(int i=0;i<raftPtr->nodeCount;++i){
        deleteRaftNode(raftPtr->nodeList[i]);
    }
    raftFree(raftPtr->nodeList);
    raftFree(raftPtr);
}

void resetRaftTime(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftPtr->timeGone = 0;
    raftPtr->electionTimeout = raftPtr->electionTimeoutBegin + (int)((raftPtr->electionTimeoutEnd - raftPtr->electionTimeoutBegin) * ((float)rand()/(float)RAND_MAX));
}

void addRaftTime(Raft* raftPtr,int add){
    raftAssert(raftPtr != NULL);
    raftPtr->timeGone += add;
}

int getRaftTime(const Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    return raftPtr->timeGone;
}

void setRaftInterface(Raft* raftPtr,Interface* interfaces,UserDataInterfaceType userData){
    raftAssert(raftPtr!=NULL);
    raftAssert(interfaces!=NULL);
    memcpy(&raftPtr->interfaces,interfaces,sizeof(Interface));
    raftPtr->userData = userData;
}

void onConversionToFollower(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftPtr->role = Follower;
    raftPtr->votedFor = (uint64_t)-1;
    raftLog(RAFT_DEBUG,"onConversionToFollower");
}

void onConversionToCandidate(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftPtr->role = Candidate;
    raftPtr->currentTerm++;
    //如果此raft实例不在最新的配置中。就不能给自己投票
    //这种情况出现在RemoveServer中，Cold中但不在Cnew中的机器可以参与选举，
    //也能够被其它机器投票。但是不能给自己投票。自己不算入majority。
    if(isNodeInConfig(raftPtr,raftPtr->nodeId)){
        raftPtr->votedFor = raftPtr->nodeId;
        raftPtr->voteMeCount = 1;
    }else{
        raftPtr->votedFor = -1;
        raftPtr->voteMeCount = 0;
    }
    
    resetRaftTime(raftPtr);

    /*发送RequestVote请求给其它机器*/
    for(int i=0;i<raftPtr->nodeCount;++i){
        //不发给自己
        if(raftPtr->nodeList[i]->nodeId == raftPtr->nodeId){
            continue;
        }
        int ret = sendRequestVote(raftPtr,raftPtr->nodeList[i]);
        if(ret != 0){
            raftLog(RAFT_WARNING,"send RequestVote to node:%lu failed",raftPtr->nodeList[i]->nodeId);
        }
    }

    raftLog(RAFT_DEBUG,"onConversionToCandidate");
}

void onConversionToLeader(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(raftPtr->role != Leader);
    raftPtr->role = Leader;
    setRaftLeaderNode(raftPtr,raftPtr->selfNode);
    for(int i=0;i<raftPtr->nodeCount;++i){
        RaftNode* node = raftPtr->nodeList[i];
        if(raftPtr->selfNode == node){
            continue;
        }
        resetRaftNodeIndex(node,logLength(raftPtr->log),logFirstLogIndex(raftPtr->log));
    }
    raftPtr->countOfLeaderCommittedEntriesInCurrentTerm = 0;
    logApppendNoop(raftPtr);

    //发送心跳包给其它机器
    sendHeartbeatsToOthers(raftPtr);
    raftLog(RAFT_DEBUG,"onConversionToLeader");
}

int isLeader(const Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    return raftPtr->role == Leader;
}

int getMajorityVote(const Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    return raftPtr->voteMeCount > (raftPtr->nodeCount/2);
}

int beyondMajority(int count,const Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    return count > (raftPtr->nodeCount/2);
}

int myLogIsNewer(const Log* logPtr,const RequestVoteRequest* reqPtr){
    raftAssert(logPtr!=NULL);
    raftAssert(reqPtr!=NULL);
    if(logLastLogTerm(logPtr) > reqPtr->lastLogTerm){
        return 1;
    }else if((logLastLogTerm(logPtr) == reqPtr->lastLogTerm) && (logLength(logPtr) > reqPtr->lastLogIndex+1)){
        return 1;
    }
    return 0;
}

void applyLog(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    //应用日志到状态机
    if(raftPtr->commitIndex > raftPtr->lastApplied){
        int count = min(raftPtr->maxCountOfApplyingEntry,raftPtr->commitIndex - raftPtr->lastApplied);
        for(int i=0;i<count;++i){
            raftPtr->lastApplied++;
            Entry e = logAt(raftPtr->log,raftPtr->lastApplied);
            if(e.type == NO_OP){
                if(e.data == NULL){
                    raftLog(RAFT_DEBUG,"raft %lu apply no_op index %d term %d cmd is null",raftPtr->nodeId,e.index,e.term);
                }else{
                    raftLog(RAFT_DEBUG,"raft %lu apply no_op index %d term %d cmd %s",raftPtr->nodeId,e.index,e.term,e.data);
                }
            }else if(e.type == CONFIG){
                raftLog(RAFT_DEBUG,"raft %lu apply config index %d term %d cmd %s",raftPtr->nodeId,e.index,e.term,e.data);
            }else{
                raftLog(RAFT_DEBUG,"raft %lu apply data index %d term %d",raftPtr->nodeId,e.index,e.term);
            }
        }
        
    }
}

int indexIsCommitted(Raft* raftPtr,int index){
    raftAssert(raftPtr != NULL);
    return raftPtr->commitIndex >= index;
}

int isLeaderCommittedEntriesInCurrentTerm(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(raftPtr->role == Leader);
    return raftPtr->countOfLeaderCommittedEntriesInCurrentTerm > 0;
}

void raftCore(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftLog(RAFT_DEBUG,"raft %lu electTimeout %d timegone %d",raftPtr->nodeId,raftPtr->electionTimeout,raftPtr->timeGone);
    if(Leader == raftPtr->role){
        //发送hearbeat给peers
        if(raftPtr->timeGone >= raftPtr->heartbeatTimeout){
            resetRaftTime(raftPtr);
            sendHeartbeatsToOthers(raftPtr);
        }
        //同步日志
        sendAppendEntriesToOthersIfNeed(raftPtr);
        //更新leaderCommit
        leaderUpdateCommitIndex(raftPtr);
    }else if(raftPtr->timeGone >= raftPtr->electionTimeout){
        //Follower和Candidate要不同处理
        if(Follower == raftPtr->role){
            //TODO:conversion to candidate
            onConversionToCandidate(raftPtr);
        }else if(Candidate == raftPtr->role){
            //TODO:如果投票数不够majority，重新选举
            if(!getMajorityVote(raftPtr)){
                onConversionToCandidate(raftPtr);
            }else{
                onConversionToLeader(raftPtr);
            }
            
        }
    }

    applyLog(raftPtr);

    checkInvariants(raftPtr);
}

void basicInvariants(const Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    //Log terms and cluster times monotonically increase
    int lastTerm = 0;
    for(int index = logFirstLogIndex(raftPtr->log);index <= logLastLogIndex(raftPtr->log);++index){
        expect(logAt(raftPtr->log,index).term >= lastTerm);
        lastTerm = logAt(raftPtr->log,index).term;
    }
    //the term in the log do not exceed currentTerm
    expect(lastTerm <= raftPtr->currentTerm);

    //the current configuration should be the last one found in the log
    //TODO:
    //Every configuration present in the log should also be present in the
    //configurationDescriptions map.
    //TODO:
    //The commitIndex doesn't exceed the length of the log/snapshot.
    expect(raftPtr->commitIndex <= logLastLogIndex(raftPtr->log));
    expect(raftPtr->lastApplied <= raftPtr->commitIndex);
    //lastLogIndex is either just below the log start(for empty logs) or larger (for non-empty logs)
    expect(logLastLogIndex(raftPtr->log) >= logFirstLogIndex(raftPtr->log) - 1);

}

void raftNodeBasicInvariants(const Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    for(int i=0;i<raftPtr->nodeCount;i++){
        const RaftNode* node = raftPtr->nodeList[i];
        expect(node->matchIndex <= logLastLogIndex(raftPtr->log));
        expect(node->matchIndex <= node->nextIndex);
    }
}

void checkInvariants(const Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    basicInvariants(raftPtr);
    raftNodeBasicInvariants(raftPtr);
}

RaftNode* getNewNode(uint64_t nodeId,void* userData){
    RaftNode *nodePtr = raftCalloc(1,sizeof(RaftNode));
    raftAssert(nodePtr!=NULL);
    nodePtr->matchIndex = 0;
    nodePtr->nextIndex = 0;
    nodePtr->appendEntriesSendTime = 0;
    nodePtr->nodeId = nodeId;
    nodePtr->userData = userData;
    return nodePtr;
}

void deleteRaftNode(RaftNode* nodePtr){
    raftAssert(nodePtr!=NULL);
    raftFree(nodePtr);
}

void setRaftNodeInterface(RaftNode* nodePtr,Interface* interfaces){
    raftAssert(nodePtr != NULL);
    if(interfaces != NULL){
        memcpy(&nodePtr->interfaces,interfaces,sizeof(Interface));
    }
}

void resetRaftNodeIndex(RaftNode* nodePtr,int nextIndex,int matchIndex){
    raftAssert(nodePtr!=NULL);
    raftAssert(nextIndex >= 1);
    raftAssert(matchIndex >= 0);
    nodePtr->nextIndex = nextIndex;
    nodePtr->matchIndex = matchIndex;
}

void showRaftNode(const RaftNode* nodePtr){
    raftAssert(nodePtr!=NULL);
    raftLog(RAFT_DEBUG,"nodeId:%lu matchIndex:%d nextIndex:%d",
        nodePtr->nodeId,nodePtr->matchIndex,nodePtr->nextIndex);
}

uint64_t getRaftNodeId(const Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    if(raftPtr->selfNode == NULL){
        return (uint64_t)(-1);
    }
    return raftPtr->selfNode->nodeId;
}

RaftNode* addRaftNodeIntoConfig(Raft* raftPtr,uint64_t nodeId,void* userData,int me){
    RaftNode* node = getRaftNodeFromConfig(raftPtr,nodeId);
    if(node){
        //已经有此机器了
        return node;
    }
    raftPtr->nodeCount++;
    raftPtr->nodeList=raftRealloc(raftPtr->nodeList,sizeof(RaftNode*) * raftPtr->nodeCount);
    raftAssert(raftPtr->nodeList!=NULL);
    raftPtr->nodeList[raftPtr->nodeCount - 1] = getNewNode(nodeId,userData);
    raftAssert(raftPtr->nodeList[raftPtr->nodeCount - 1] != NULL);
    setRaftNodeInterface(raftPtr->nodeList[raftPtr->nodeCount - 1],&raftPtr->interfaces);
    if(me){
        raftPtr->nodeId = nodeId;
        raftPtr->selfNode = raftPtr->nodeList[raftPtr->nodeCount - 1];
    }
    //对新增的node的index，进行复位
    resetRaftNodeIndex(raftPtr->nodeList[raftPtr->nodeCount - 1],1,0);
    return raftPtr->nodeList[raftPtr->nodeCount - 1];
}

void removeRaftNodeFromConfig(Raft* raftPtr,uint64_t nodeId){
    raftAssert(raftPtr != NULL);
    for (int i = 0; i < raftPtr->nodeCount; i++)
    {
        if(nodeId == raftPtr->nodeList[i]->nodeId){
            //与尾部节点交换
            if(i < raftPtr->nodeCount - 1){
                RaftNode* t = raftPtr->nodeList[raftPtr->nodeCount-1];
                raftPtr->nodeList[raftPtr->nodeCount-1] = raftPtr->nodeList[i];
                raftPtr->nodeList[i] = t;
            }
            //从尾部删除节点
            deleteRaftNode(raftPtr->nodeList[ raftPtr->nodeCount - 1 ]);
            raftPtr->nodeList[ raftPtr->nodeCount - 1 ] = NULL;
            raftPtr->nodeCount--;
            break;
        }
    }
}

RaftNode* getRaftNodeFromConfig(const Raft* raftPtr,uint64_t nodeId){
    raftAssert(raftPtr != NULL);
    for (int i = 0; i < raftPtr->nodeCount; i++)
    {
        if(nodeId == raftPtr->nodeList[i]->nodeId){
            return raftPtr->nodeList[i];
        }
    }
    return NULL;
}

void setRaftLeaderNode(Raft* raftPtr,RaftNode* nodePtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr !=NULL);
    raftPtr->leaderNode = nodePtr;
}

int sendRequestVote(Raft* raftPtr,RaftNode* nodePtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(nodePtr != raftPtr->selfNode);

    RequestVoteRequest req;
    req.term = raftPtr->currentTerm;
    req.candidateId = getRaftNodeId(raftPtr);
    req.lastLogIndex = logLastLogIndex(raftPtr->log);
    req.lastLogTerm = logLastLogTerm(raftPtr->log);

    int ret = 0;
    if(raftPtr->interfaces.sendRequestVoteRequest){
        ret = raftPtr->interfaces.sendRequestVoteRequest(raftPtr,nodePtr,raftPtr->userData,&req);
    }

    return ret;
}

int getAppendEntriesRequestLength(const AppendEntriesRequest* reqPtr){
    raftAssert(reqPtr!=NULL);
    int len = 0;
    len += sizeof(AppendEntriesRequest);
    for(int i=0;i<reqPtr->entryCount;++i){
        len += getEntrySpaceLength(reqPtr->entries+i);
    }
    return len;
}

int followerRecvRequestVote(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteRequest* reqPtr,RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    if(reqPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(reqPtr->term < raftPtr->currentTerm){
        respPtr->term = raftPtr->currentTerm;
        respPtr->voteGranted = 0;
    }else{
        raftAssert(reqPtr->term == raftPtr->currentTerm);
        respPtr->term = raftPtr->currentTerm;
        if((raftPtr->votedFor == (uint64_t)-1 || raftPtr->votedFor == reqPtr->candidateId) && !myLogIsNewer(raftPtr->log,reqPtr)){
            respPtr->voteGranted = 1;
            raftPtr->votedFor = reqPtr->candidateId;
            //TODO:再次确认
            resetRaftTime(raftPtr);
            raftLog(RAFT_DEBUG,"follower(term %d lastLogTerm %d loglen %d) vote node %lu (term %d lastLogTerm %d loglen %d)",
                raftPtr->currentTerm,logLastLogTerm(raftPtr->log),logLength(raftPtr->log),
                reqPtr->candidateId,
                reqPtr->term,reqPtr->lastLogTerm,reqPtr->lastLogIndex+1
                );
        }else{
            respPtr->voteGranted = 0;
        }
    }

    return 0;
}
int candidateRecvRequestVote(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteRequest* reqPtr,RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    if(reqPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(reqPtr->term < raftPtr->currentTerm){
        respPtr->term = raftPtr->currentTerm;
        respPtr->voteGranted = 0;
    }else{
        raftAssert(reqPtr->term == raftPtr->currentTerm);
        respPtr->term = raftPtr->currentTerm;
        if((raftPtr->votedFor == (uint64_t)-1 || raftPtr->votedFor == reqPtr->candidateId) && !myLogIsNewer(raftPtr->log,reqPtr)){
            respPtr->voteGranted = 1;
            raftPtr->votedFor = reqPtr->candidateId;
            //TODO:再次确认
            resetRaftTime(raftPtr);
            raftLog(RAFT_DEBUG,"candidate(term %d loglen %d) vote node %lu (term %d loglen %d)",
                logLastLogTerm(raftPtr->log),logLength(raftPtr->log),
                reqPtr->candidateId,
                reqPtr->lastLogTerm,reqPtr->lastLogIndex+1
                );
        }else{
            respPtr->voteGranted = 0;
        }
    }
    return 0;
}
int leaderRecvRequestVote(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteRequest* reqPtr,RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    if(reqPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(reqPtr->term < raftPtr->currentTerm){
        respPtr->term = raftPtr->currentTerm;
        respPtr->voteGranted = 0;
    }else{
        raftAssert(reqPtr->term == raftPtr->currentTerm);
        respPtr->term = raftPtr->currentTerm;
        if((raftPtr->votedFor == (uint64_t)-1 || raftPtr->votedFor == reqPtr->candidateId) && !myLogIsNewer(raftPtr->log,reqPtr)){
            respPtr->voteGranted = 1;
            raftPtr->votedFor = reqPtr->candidateId;
            //TODO:再次确认
            resetRaftTime(raftPtr);
            raftLog(RAFT_DEBUG,"leader(term %d loglen %d) vote node %lu (term %d loglen %d)",
                logLastLogTerm(raftPtr->log),logLength(raftPtr->log),
                reqPtr->candidateId,
                reqPtr->lastLogTerm,reqPtr->lastLogIndex+1
                );
        }else{
            respPtr->voteGranted = 0;
        }
    }
    return 0;
}

int recvRequestVote(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteRequest* reqPtr,RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    while(1){
        if(raftPtr->role == Follower){
            int again = followerRecvRequestVote(raftPtr,nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Candidate){
            int again = candidateRecvRequestVote(raftPtr,nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Leader){
            int again = leaderRecvRequestVote(raftPtr,nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else{
            raftLog(RAFT_WARNING,"Wrong role of the Raft. receiving RequestVote");
            break;
        }
    }
    return 0;
}

int followerRecvRequestVoteResponse(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);

    //之前是candidate时，发送的ReqestVote，现在得到回复
    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        return 0;
    }else
    {
        return 0;
    }
    
    return 0;
}

int candidateRecvRequestVoteResponse(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);

    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        return 0;
    }else
    {
        /*
        if currentTerm != req.Term drop rpc;
        */
       if(raftPtr->currentTerm != respPtr->requestTerm){
           return 0;
       }else
       {
           if(respPtr->voteGranted){
               //增加投票数
               raftPtr->voteMeCount++;
           }
       }
    }

    //如投票数够数，就切换到leder
    if(getMajorityVote(raftPtr)){
        onConversionToLeader(raftPtr);
    }
    return 0;
}

int leaderRecvRequestVoteResponse(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);

    //之前是candidate时，发送的ReqestVote，现在得到回复
    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        return 0;
    }else
    {
        //已经是leader，多余的投票
        return 0;
    }
    return 0;
}

int recvRequestVoteResponse(Raft* raftPtr,const RaftNode* nodePtr,const RequestVoteResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);

    while(1){
        if(raftPtr->role == Follower){
            int again = followerRecvRequestVoteResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Candidate){
            int again = candidateRecvRequestVoteResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Leader){
            int again = leaderRecvRequestVoteResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else{
            raftLog(RAFT_WARNING,"Wrong role of the Raft. receiving RequestVoteResponse");
            break;
        }
    }
    return 0;
}

int followerRecvAppendEntriesRequest(Raft* raftPtr,RaftNode* nodePtr,const AppendEntriesRequest* reqPtr,AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    respPtr->prevLogIndex = reqPtr->prevLogIndex;
    respPtr->entryCount = reqPtr->entryCount;
    if(reqPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        resetRaftTime(raftPtr);
        return 1;
    }else if(reqPtr->term < raftPtr->currentTerm){
        respPtr->term = raftPtr->currentTerm;
        respPtr->success = 0;
    }else{
        raftAssert(reqPtr->term == raftPtr->currentTerm);
        resetRaftTime(raftPtr);
        respPtr->term = raftPtr->currentTerm;
        //leader的prevLogIndex超出本地日志范围。没得比。
        if(reqPtr->prevLogIndex >= logLength(raftPtr->log)){
            respPtr->success = 0;
            respPtr->conflictTerm = -1;
            respPtr->firstIndexOfConflictTerm = logLength(raftPtr->log);
        }else if(reqPtr->prevLogIndex <= logLastIncludedIndex(raftPtr->log)){
            //此种情况出现在本实例生成快照之后。leader的prevLogIndex落在快照范围内。
            //快照范围内的Entry无需再比较。但是快照范围外的Entry要重新比较。
            //从快照范围外的Entry处开始比较
            int leaderIndex = reqPtr->prevLogIndex+1;
            int newIndex = max(leaderIndex,logLastIncludedIndex(raftPtr->log)+1);
            int offset = leaderIndex;
            int newLen = min(logLength(raftPtr->log),offset+reqPtr->entryCount);
            //it must be the index of last new entry
            int indexOfLastNewEntry = logLastIncludedIndex(raftPtr->log)+1;
            for(;newIndex < newLen;++newIndex){
                if((logAt(raftPtr->log,newIndex).index == reqPtr->entries[newIndex-offset].index) && 
                        (logAt(raftPtr->log,newIndex).term == reqPtr->entries[newIndex-offset].term)){
                    continue;
                }else{
                    break;
                }
            }
            //delete all conflicting entries and apply new entries
            if(newIndex < newLen){
                //有冲突Entry
                //删除冲突及其之后的全部Entry
                int delCnt = logLength(raftPtr->log) - newIndex;
                logDeleteAtEnd(raftPtr->log,delCnt);
                //拼接上leader的Entry
                for(int index = newIndex - offset;index < reqPtr->entryCount;++index){
                    logAppend(raftPtr,reqPtr->entries[index]);
                }
                indexOfLastNewEntry = newLen - 1;
            }else if(logLength(raftPtr->log) < offset + reqPtr->entryCount){
                //没有冲突Entry
                //拼接新增的Entry
                for(int index = newIndex - offset; index < reqPtr->entryCount;++index){
                    logAppend(raftPtr,reqPtr->entries[index]);
                }
                indexOfLastNewEntry = logLength(raftPtr->log) - 1;
            }else{
                //没有冲突Entry。也没有新增的Entry
                indexOfLastNewEntry = offset + reqPtr->entryCount - 1;
            }

            if(reqPtr->leaderCommit > raftPtr->commitIndex){
                int nextCommitIndex = min(reqPtr->leaderCommit,indexOfLastNewEntry);
                raftPtr->commitIndex = max(raftPtr->commitIndex,nextCommitIndex);
            }

            respPtr->success = 1;
        }else if(logAt(raftPtr->log,reqPtr->prevLogIndex).term != reqPtr->prevLogTerm){
            //prevLogIndex处Entry冲突
            respPtr->success = 0;
            respPtr->conflictTerm = logAt(raftPtr->log,reqPtr->prevLogIndex).term;
            //用冲突的term，在日志中向前找出第一个不等于冲突term的term的位置
            int i=reqPtr->prevLogIndex;
            for(;i >= (logLastIncludedIndex(raftPtr->log)+1);i--){
                if(logAt(raftPtr->log,i).term != logAt(raftPtr->log,reqPtr->prevLogIndex).term){
                    break;
                }
            }
            respPtr->firstIndexOfConflictTerm = i+1;
        }else{
            int newIndex = reqPtr->prevLogIndex + 1;
            int offset = newIndex;
            int newLen = min(logLength(raftPtr->log),offset+reqPtr->entryCount);
            //it must be the index of last new entry
            int indexOfLastNewEntry = logLastIncludedIndex(raftPtr->log)+1;
            //find entry conflicts
            for(;newIndex < newLen;++newIndex){
                if((logAt(raftPtr->log,newIndex).index == reqPtr->entries[newIndex - offset].index) &&
                        (logAt(raftPtr->log,newIndex).term == reqPtr->entries[newIndex - offset].term)){
                    continue;
                }else{
                    break;
                }
            }
            //delete all conflicting entries and apply new entries
            if(newIndex < newLen){
                //有冲突Entry
                //删除冲突及其之后的全部entry
                int delCnt = logLength(raftPtr->log) - newIndex;
                logDeleteAtEnd(raftPtr->log,delCnt);
                //拼接上leader的日志
                for(int index = newIndex - offset;index < reqPtr->entryCount;index++){
                    logAppend(raftPtr,reqPtr->entries[index]);
                }
                indexOfLastNewEntry = newLen - 1;
            }else if(logLength(raftPtr->log) < offset + reqPtr->entryCount){
                //没有冲突Entry
                //拼接上新增的Entry
                for(int index = newIndex - offset;index < reqPtr->entryCount;index++){
                    logAppend(raftPtr,reqPtr->entries[index]);
                }
                indexOfLastNewEntry = logLength(raftPtr->log) - 1;
            }else{
                indexOfLastNewEntry = offset + reqPtr->entryCount - 1;
            }

            if(reqPtr->leaderCommit > raftPtr->commitIndex){
                int nextCommitIndex = min(reqPtr->leaderCommit,indexOfLastNewEntry);
                raftPtr->commitIndex = max(raftPtr->commitIndex,nextCommitIndex);
            }
            respPtr->success = 1;
        }
    }

    setRaftLeaderNode(raftPtr,nodePtr);
    return 0;
}

int candidateRecvAppendEntriesRequest(Raft* raftPtr,const RaftNode* nodePtr,const AppendEntriesRequest* reqPtr,AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    respPtr->prevLogIndex = reqPtr->prevLogIndex;
    respPtr->entryCount = reqPtr->entryCount;
    if(reqPtr->term >= raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        resetRaftTime(raftPtr);
        return 1;
    }else{
        //已有leader的term比currentTerm小，返回false
        respPtr->term = raftPtr->currentTerm;
        respPtr->success = 0;
    }

    return 0;
}

int leaderRecvAppendEntriesRequest(Raft* raftPtr,const RaftNode* nodePtr,const AppendEntriesRequest* reqPtr,AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    respPtr->requestTerm = reqPtr->term;
    respPtr->prevLogIndex = reqPtr->prevLogIndex;
    respPtr->entryCount = reqPtr->entryCount;
    if(reqPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        resetRaftTime(raftPtr);
        return 1;
    }else if(reqPtr->term < raftPtr->currentTerm){
        respPtr->term = raftPtr->currentTerm;
        respPtr->success = 0;
    }else{
        raftPtr->currentTerm = reqPtr->term;
        onConversionToFollower(raftPtr);
        resetRaftTime(raftPtr);
        return 1;
    }
    return 0;
}

int recvAppendEntriesRequest(Raft* raftPtr,const RaftNode* nodePtr,const AppendEntriesRequest* reqPtr,AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(reqPtr != NULL);
    raftAssert(respPtr!=NULL);
    while(1){
        if(raftPtr->role == Follower){
            int again = followerRecvAppendEntriesRequest(raftPtr,(RaftNode*)nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Candidate){
            int again = candidateRecvAppendEntriesRequest(raftPtr,nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Leader){
            int again = leaderRecvAppendEntriesRequest(raftPtr,nodePtr,reqPtr,respPtr);
            if(again){
                continue;
            }else break;
        }else{
            raftLog(RAFT_WARNING,"Wrong role of the Raft. receiving AppendEntries");
            break;
        }
    }
    return 0;
}

int sendAppendEntries(Raft* raftPtr,RaftNode* nodePtr,int entryCount){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(nodePtr != raftPtr->selfNode);

    AppendEntriesRequest req;
    req.term = raftPtr->currentTerm;
    req.leaderId = getRaftNodeId(raftPtr);
    req.prevLogIndex = nodePtr->nextIndex - 1;
    req.prevLogTerm = logAt(raftPtr->log,req.prevLogIndex).term;
    req.leaderCommit = raftPtr->commitIndex;

    if(entryCount == 0){
        req.entries = NULL;
        req.entryCount = 0;
    }else{
        const int length = min(logLength(raftPtr->log),nodePtr->nextIndex + entryCount);
        //要复制的Entry个数
        int count = length - nodePtr->nextIndex;
        raftAssert(count >= 0);
        if(count > 0){
            req.entries = raftCalloc(count, sizeof(Entry));
            raftAssert(req.entries != NULL);
            for(int j=nodePtr->nextIndex;j < length;++j){
                req.entries[j - nodePtr->nextIndex] = logAt(raftPtr->log,j);
                //TEST
                raftAssert(logAt(raftPtr->log,j).data != NULL && logAt(raftPtr->log,j).dataLen != 0);
            }
            req.entryCount = count;
        }else{
            req.entries = NULL;
            req.entryCount = 0;
        }
    }

    int ret = 0;
    if(raftPtr->interfaces.sendAppendEntriesRequest){
        ret = raftPtr->interfaces.sendAppendEntriesRequest(raftPtr,nodePtr,raftPtr->userData,&req);
    }

    raftFree(req.entries);
    //记录发送给机器的时间
    nodePtr->appendEntriesSendTime = getRaftTime(raftPtr);
    return ret;
}

int sendHeartbeatsToOthers(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(raftPtr->role == Leader);
    for(int i=0;i<raftPtr->nodeCount;++i){
        if(raftPtr->selfNode == raftPtr->nodeList[i]){
            continue;
        }
        if(raftPtr->nodeList[i]->nextIndex <= logLastIncludedIndex(raftPtr->log)){
            //发送快照
            raftPanic("snapshot is not supported");
        }else{
            sendAppendEntries(raftPtr,raftPtr->nodeList[i],0);
        }
        
    }
    return 0;
}

int sendAppendEntriesToOthersIfNeed(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(raftPtr->role == Leader);
    for(int i=0;i<raftPtr->nodeCount;++i){
        if(raftPtr->selfNode == raftPtr->nodeList[i]){
            continue;
        }
        if(raftPtr->nodeList[i]->nextIndex <= logLastIncludedIndex(raftPtr->log)){
            //发送快照
            raftPanic("snapshot is not supported");
        }else if(logLength(raftPtr->log)-1 >= raftPtr->nodeList[i]->nextIndex){
            //流量控制
            if(getRaftTime(raftPtr) - raftPtr->nodeList[i]->appendEntriesSendTime >= raftPtr->maxAppendEntriesSendInterval){
                sendAppendEntries(raftPtr,raftPtr->nodeList[i],raftPtr->maxEntryCountInOneAppendEntries);
            }
        }
    }
    return 0;
}

void leaderUpdateCommitIndex(Raft* raftPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(raftPtr->role == Leader);
    for(int N=raftPtr->commitIndex+1;N < logLength(raftPtr->log);N++){
        if(logAt(raftPtr->log,N).term != raftPtr->currentTerm){
            continue;
        }
        int cnt = 0;
        //如果此raft实例不在最新的配置中。就不能给自己投票。
        //这种情况出现在RemoveServer中，leader在Cold中但不在Cnew中，可以参与选举，
        //也能够被其它机器投票。可以复制日志给其它机器。
        //但是不能给自己投票。自己不算入majority。
        if(isNodeInConfig(raftPtr,raftPtr->nodeId)){
            cnt = 1;
        }
        for(int i=0;i<raftPtr->nodeCount;i++){
            if(raftPtr->nodeList[i] == raftPtr->selfNode){
                continue;
            }
            if(raftPtr->nodeList[i]->matchIndex >= N){
                ++cnt;
            }
        }
        if(beyondMajority(cnt,raftPtr)){
            raftPtr->commitIndex = N;
            raftPtr->countOfLeaderCommittedEntriesInCurrentTerm++;
        }
    }

    if(indexIsCommitted(raftPtr,getLastConfigEntryIndex(raftPtr->log))){
        //看是否有关联的pending的AddRemoveServer请求
        processPendigAddRemoveServerRequest(raftPtr);
    }
}

int followerRecvAppendEntriesResponse(Raft* raftPtr,const RaftNode* nodePtr,const AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);
    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        return 0;
    }else{
        return 0;
    }

    return 0;
}

int candidateRecvAppendEntriesResponse(Raft* raftPtr,const RaftNode* nodePtr,const AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);
    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        return 0;
    }else{
        return 0;
    }

    return 0;
}

int leaderRecvAppendEntriesResponse(Raft* raftPtr,RaftNode* nodePtr,const AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);
    if(respPtr->requestTerm != raftPtr->currentTerm){
        return 0;
    }else if(respPtr->term > raftPtr->currentTerm){
        raftPtr->currentTerm = respPtr->term;
        onConversionToFollower(raftPtr);
        return 1;
    }else if(respPtr->term < raftPtr->currentTerm){
        //之前是leader时，发送的AppendEntries，现在才得到回复
        return 0;
    }else if(respPtr->success){
        //收到肯定答复。增加（可能不变，拒绝减小）机器的matchIndex和nextIndex。
        int prev = nodePtr->matchIndex;
        //此处新值为，此response对应的AppendEntries请求中的prevLogIndex和entryCount
        int next = respPtr->prevLogIndex + respPtr->entryCount;
        next = max(next,prev);
        nodePtr->matchIndex = next;
        nodePtr->nextIndex = max(nodePtr->matchIndex+1,nodePtr->nextIndex);
    }else{
        //收到肯定答复。减少（可能不变，拒绝增加）机器的matchIndex和nextIndex。
        int next = nodePtr->nextIndex;
        if(respPtr->conflictTerm != -1){
            //有冲突的term
            int i= respPtr->prevLogIndex;
            //向前查找到冲突term的最后一个entry的索引
            for(;i>=(logLastIncludedIndex(raftPtr->log) + 1);--i){
                if(logAt(raftPtr->log,i).term == respPtr->conflictTerm){
                    break;
                }
            }
            //找到conflictTerm最后一个entry
            if(i >= (logLastIncludedIndex(raftPtr->log) + 1)){
                //找到
                next = i+1;
            }else{
                //没找到
                next = respPtr->firstIndexOfConflictTerm;
            }
        }else
        {
            next = respPtr->firstIndexOfConflictTerm;
        }
        
        next = min(next,nodePtr->nextIndex);
        nodePtr->nextIndex = max(1,next);
    }
    return 0;
}

int recvAppendEntriesResponse(Raft* raftPtr,RaftNode* nodePtr,const AppendEntriesResponse* respPtr){
    raftAssert(raftPtr != NULL);
    raftAssert(nodePtr != NULL);
    raftAssert(respPtr!=NULL);
    while(1){
        if(raftPtr->role == Follower){
            int again = followerRecvAppendEntriesResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Candidate){
            int again = candidateRecvAppendEntriesResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else if(raftPtr->role == Leader){
            int again = leaderRecvAppendEntriesResponse(raftPtr,nodePtr,respPtr);
            if(again){
                continue;
            }else break;
        }else{
            raftLog(RAFT_WARNING,"Wrong role of the Raft. receiving AppendEntriesResponse");
            break;
        }
    }
    return 0;
}

int submitDataToRaft(Raft* raftPtr,sds data,int dataLen,enum EntryType type,int* index,int* term){
    raftAssert(raftPtr!=NULL);
    if(CONFIG != type){
        if(!isLeader(raftPtr)){
            raftLog(RAFT_WARNING,"not leader. don't receiver data");
            return -1;
        }
    }

    int logIndex = logAppendData(raftPtr,data,dataLen,type);
    if(index){
        *index = logIndex;
    }
    int logTerm = logAt(raftPtr->log,logIndex).term;
    if(term){
        *term = logTerm;
    }
    return 0;
}

int addServer(Raft* raftPtr,uint64_t nodeId,UserDataInterfaceType userData){
    raftAssert(raftPtr!=NULL);
    AddRemoveServerRequest req;
    req.method = ADD;
    req.nodeId = nodeId;
    if(userData == NULL){
        req.userData = NULL;
        req.userDataLen = 0;
    }else{
        if(raftPtr->interfaces.addRemoveServerUserDataToString){
            req.userData = NULL;
            raftPtr->interfaces.addRemoveServerUserDataToString(userData,&req.userData);
            req.userDataLen = sdslen(req.userData);
        }else{
            req.userData = NULL;
            req.userDataLen = 0;
        }
    }
    //TODO:发送
    for(int i=0;i<raftPtr->nodeCount;i++){
        RaftNode* nodePtr = raftPtr->nodeList[i];
        //此raft实例
        if(nodePtr->nodeId == raftPtr->nodeId){
            //TODO:发送给自己
            continue;
        }
        if(raftPtr->interfaces.sendAddRemoveServerRequest){
            raftPtr->interfaces.sendAddRemoveServerRequest(raftPtr,nodePtr->nodeId,nodePtr->userData,&req);
        }
    }

    sdsfree(req.userData);
    return 0;
}

int recvAddRemoveServerRequest(Raft* raftPtr,UserDataInterfaceType userData,
        const AddRemoveServerRequest* reqPtr,AddRemoveServerResponse* respPtr){
    raftAssert(raftPtr!=NULL);
    raftAssert(userData!=NULL);
    raftAssert(reqPtr!=NULL);
    raftAssert(respPtr!=NULL);
        if(raftPtr->role != Leader){
        respPtr->status = NOT_LEADER;
        return 1;
    }

    if(reqPtr->method == ADD){
        if(isNodeInConfig(raftPtr,reqPtr->nodeId)){
            respPtr->status = SERVER_IN_CONFIG_ALREADY;
            return 1;
        }
    }else if(reqPtr->method == REMOVE){
        if(! isNodeInConfig(raftPtr,reqPtr->nodeId)){
            respPtr->status = SERVER_NOT_IN_CONFIG;
            return 1;
        }
    }else{
        raftLog(RAFT_WARNING,"unknown method");
        respPtr->status = UNKNOWN_METHOD;
        return 1;
    }

    if(!indexIsCommitted(raftPtr,getLastConfigEntryIndex(raftPtr->log))){
        //之前的配置修改还没commit
        //TODO:关联到pending请求中
        respPtr->status = WAIT_PREVIOUS_CONFIG_COMMITTED;
        return 1;
    }else{
        if(raftPtr->pendingAddRemoveServerRequest){
            //还有关联的pending请求没有答复
            respPtr->status = WAIT_PENDING_REQUEST_ANSWERED;
            return 1;
        }
        //如果leader在currentTerm中没有commit过任何entry。先commit一个entry后
        //才能再更新机器配置
        if(!isLeaderCommittedEntriesInCurrentTerm(raftPtr)){
            logApppendNoop(raftPtr);
            //TODO:关联到pending请求中
            respPtr->status = WAIT_NO_OP_ENTRY_COMMITTED_IN_COLD;
            return 1;
        }
        
        uint64_t nodeId = reqPtr->nodeId;
        
        //之前的配置修改已committed，并且没有关联的pending请求
        //Append Cnew into log
        if(reqPtr->method == ADD){
            //对机器字符串空格拆分
            int count;
            sds* lines = sdssplitlen(reqPtr->userData,reqPtr->userDataLen," ",1,&count);
            UserDataInterfaceType addr = NULL;
            //讲机器字符串数组转为userData
            raftPtr->interfaces.raftNodeStringToUserData(lines,count,&addr);
            sds cnew=makeConfigStringWith(raftPtr,nodeId,addr);
            logAppendData(raftPtr,cnew,sdslen(cnew),CONFIG);
            sdsfree(cnew);
            raftFree(addr);
            sdsfreesplitres(lines,count);
        }else if(reqPtr->method == REMOVE){
            sds cnew=makeConfigStringWithout(raftPtr,nodeId);
            logAppendData(raftPtr,cnew,sdslen(cnew),CONFIG);
            sdsfree(cnew);
        }

        //Cnew还没commit
        if(!indexIsCommitted(raftPtr,getLastConfigEntryIndex(raftPtr->log))){
            //TODO:关联到pending请求中
            //等到日志被commit时，再答复
            AddRemoveServerRequest *pending = raftMalloc(sizeof(AddRemoveServerRequest));
            raftAssert(pending!=NULL);
            *pending = *reqPtr;
            raftPtr->pendingAddRemoveServerRequest = pending;
            //node可能是NULL。但是Peer一定不是NULL
            raftPtr->pendingAddRemoveServerPeer = userData;
            return 0;
        }
        respPtr->status = OK;
        return 1;
    }
    return 0;
}

void processPendigAddRemoveServerRequest(Raft* raftPtr){
    raftAssert(raftPtr!=NULL);
    if(raftPtr->pendingAddRemoveServerRequest && raftPtr->pendingAddRemoveServerPeer){
        AddRemoveServerResponse hostResp;
        hostResp.status = OK;
        if(raftPtr->interfaces.sendAddRemoveServerResponse){
            raftPtr->interfaces.sendAddRemoveServerResponse(raftPtr,raftPtr->pendingAddRemoveServerPeer,&hostResp);
        }
        
        raftFree(raftPtr->pendingAddRemoveServerRequest);
        raftPtr->pendingAddRemoveServerRequest = NULL;
        raftPtr->pendingAddRemoveServerPeer = NULL;
    }
}

//==========================================================
int max(int a,int b){
    if(a > b) return a;
    else return b;
}

int min(int a,int b){
    if(a < b) return a;
    else return b;
}

void raftLog(int level,const char* fmt,...){
    if(level){
        level += 0;
    }
    char buffer[4096];
    va_list args;
    va_start(args,fmt);
    vsnprintf(buffer,sizeof(buffer),fmt,args);
    fprintf(stderr,"%s\n",buffer);
}

void raftPanic(const char* fmt,...){
    char buffer[4096];
    va_list args;
    va_start(args,fmt);
    vsnprintf(buffer,sizeof(buffer),fmt,args);
    fprintf(stderr,"%s\n",buffer);
    assert(0);
}