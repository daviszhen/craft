#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "simulator.h"

using namespace Simulator;

int main(int argc,char** argv){
    srand(time(NULL));
    Cluster * cluster = new Cluster;
    Interface interfaces;
    memset(&interfaces,0,sizeof(Interface));
    interfaces.sendRequestVoteRequest = sendRequestVoteRequestInCluster;
    interfaces.sendAppendEntriesRequest = sendAppendEntriesRequestInCluster;

    const int nodeCount = 3;
    std::vector<Raft*> rafts(nodeCount);
    for(int i=0;i<nodeCount;i++){
        uint64_t nodeId = cluster->getNextNodeId();
        Node* node = new Node(nodeId);
        node->setNodeCluster(cluster);
        addRaftNodeIntoConfig(node->raft,nodeId,node,1);
        //Raft的userdata指向所在的节点
        setRaftInterface(node->raft,&interfaces,node);
        rafts[i] = node->raft;
        cluster->addNode(node);
    }

    sds config =  sdsempty();
    //生成初始配置
    for(int i=0;i<rafts.size();++i){
        config = sdscatfmt(config,"%U 1 ",rafts[i]->nodeId);
    }

    //提交配置
    for(int i=0;i<rafts.size();i++){
        submitDataToRaft(rafts[i],config,strlen(config),CONFIG,NULL,NULL);
    }

    while(1){
        cluster->core();
        cluster->dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    sdsfree(config);
    delete cluster;
    return 0;
}