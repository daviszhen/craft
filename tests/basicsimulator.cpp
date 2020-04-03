#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <chrono>

#include "simulator.h"

DEFINE_int32(raftLogLevel,RAFT_WARNING,"log level in Raft. default RAFT_DEBUG ");
DEFINE_bool(fastTest,true,"test without sleeping. default false");
DEFINE_int32(livenessCheckPeriod,3,"period that tests liveness. default 3minutes");
DEFINE_int32(printingPeriod,30,"period that print statistics info. default 30seconds");
DEFINE_int32(testTime,6,"testing time. default 6minutes");


using namespace Simulator;

int main(int argc,char** argv){
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    srand(time(NULL));
    Cluster * cluster = new Cluster;
    Interface interfaces;
    memset(&interfaces,0,sizeof(Interface));
    interfaces.sendRequestVoteRequest = sendRequestVoteRequestInCluster;
    interfaces.sendAppendEntriesRequest = sendAppendEntriesRequestInCluster;
    interfaces.addingEntry = addingEntryInCluster;
    interfaces.deletingEntryAtEnd = deletingEntryAtEndInCluster;
    interfaces.deletingEntryAtBegin = deletingEntryAtBeginInCluster;
    interfaces.applyEntry = applyLogInCluster;
    interfaces.getConfigEntryNodeId = getConfigEntryNodeIdInCluster;
    interfaces.membershipChangeEvent = membershipChangeEventInCluster;
    interfaces.onConversionToLeaderBefore = onConversionToLeaderBeforeInCluster;
    interfaces.onConversionToLeaderAfter = onConversionToLeaderAfterInCluster;
    interfaces.persistState = persistStateInCluster;

    memcpy(&cluster->raftInterfaces,&interfaces,sizeof(Interface));

    raftLogLevel = FLAGS_raftLogLevel;

    const int nodeCount = FLAGS_nodeCount;
    std::vector<Raft*> rafts(nodeCount);
    for(int i=0;i<nodeCount;i++){
        uint64_t nodeId = cluster->getNextNodeId();
        Node* node = new Node(nodeId);
        assert(cluster->addNode(node));
        node->setNodeCluster(cluster);
        addRaftNodeIntoConfig(node->raft,nodeId,node,1);
        //Raft的userdata指向所在的节点
        setRaftInterface(node->raft,&interfaces,node);
        rafts[i] = node->raft;
    }

    sds config =  sdsempty();
    //生成初始配置
    for(int i=0;i<rafts.size();++i){
        config = sdscatfmt(config,"%U 1 ",rafts[i]->nodeId);
    }

    cluster->raftConfig = sdsdup(config);

    //提交配置
    for(int i=0;i<rafts.size();i++){
        submitDataToRaft(rafts[i],config,strlen(config),LOAD_CONFIG,NULL,NULL);
    }
    int clock = 0;
    //活性检测间隔 3分钟
    const int livenessCheck = FLAGS_livenessCheckPeriod * 60 * 1000;//ms
    const int ms10 = 10;
    const int ms300 = 300;
    int sleepTime;
    if(FLAGS_raftLogLevel == RAFT_WARNING){
        sleepTime = ms10;
    }else
    {
        sleepTime = ms300;
    }

    if(FLAGS_fastTest){
        cluster->setLivenessCheckThreshold(livenessCheck);
    }else{
        cluster->setLivenessCheckThreshold(livenessCheck / sleepTime);
    }
    
    auto lastTime = std::chrono::steady_clock::now();
    auto beginTime = lastTime;
    int loop = 0;
    while(1){
        ++clock;
        cluster->setClusterClock(clock);
        cluster->core();
        cluster->dispatch();
        cluster->checkElectionSafety();

        if(!FLAGS_fastTest){
            if(FLAGS_raftLogLevel == RAFT_WARNING){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }else{
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                std::cout << std::endl;
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        //超过30s，打印一次统计信息
        if(lastTime + std::chrono::seconds(FLAGS_printingPeriod) < end){
            lastTime = end;
            fprintf(stderr,"loop%d ======================================================================\n",loop);
            cluster->printNodes();
            cluster->printRebootingNodes();
            fprintf(stderr,"loop%d ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n",loop);
            loop++;
        }
        //超过6分钟，退出
        if(beginTime + std::chrono::minutes(FLAGS_testTime) < end){
            cluster->printNodes();
            printf("normal exit...\n");
            break;
        }
    }

    sdsfree(config);
    delete cluster;
    return 0;
}