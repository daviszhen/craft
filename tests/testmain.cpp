#include "raft.h"
#include "logger.h"
#include <gflags/gflags.h>

int main(){
    Raft * raft = getNewRaft();
    deleteRaft(raft);
    return 0;
}