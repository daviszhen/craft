#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ftw.h>

#include "lmdbwrapper.h"

namespace Lmdb{

void createLmdbEnv(MDB_env** envPtr,unsigned int flags,const char* pathPtr,int dbMbSize){
    assert(envPtr!=nullptr);
    assert(pathPtr!=nullptr);
    assert(dbMbSize > 0);
    int ret = mkdir(pathPtr,0777);
    if(ret != 0){
        perror("create_lmdb_env mkdir failed.");
        assert(0);
    }

    //创建环境句柄
    ret = mdb_env_create(envPtr);
    lmdbAssertIsZero(ret);

    ret = mdb_env_set_maxdbs(*envPtr,10);
    lmdbAssertIsZero(ret);

    ret = mdb_env_set_mapsize(*envPtr,dbMbSize * 1024 * 1024);
    lmdbAssertIsZero(ret);

    //打开环境句柄
    ret = mdb_env_open(*envPtr,pathPtr,flags,0664);
    lmdbAssertIsZero(ret);
}

void createLmdbDb(MDB_env* envPtr,MDB_dbi* dbPtr,const char* dbNamePtr,int flags){
    assert(envPtr!=nullptr);
    assert(dbPtr!=nullptr);
    assert(dbNamePtr!=nullptr);

    int ret = 0;
    MDB_txn *txn = nullptr;
    ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);

    //创建数据库包含在事务中
    ret = mdb_dbi_open(txn,dbNamePtr,MDB_CREATE|flags,dbPtr);
    lmdbAssertIsZero(ret);

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);
}

int lmdbPut(MDB_env* envPtr,MDB_dbi db,MDB_val key,MDB_val value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    MDB_txn *txn = nullptr;
    int ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);

    ret = mdb_put(txn,db,&key,&value,0);
    lmdbAssertIsZero(ret);

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    return 0;
}

int lmdbPutStringInt(MDB_env* envPtr,MDB_dbi db,char* key,int value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    MDB_val mkey;
    mkey.mv_data = key;
    mkey.mv_size = strlen(key);

    MDB_val mvalue;
    mvalue.mv_data = &value;
    mvalue.mv_size = sizeof(value);

    lmdbPut(envPtr,db,mkey,mvalue);

    return 0;
}

int lmdbPutStringUint64(MDB_env* envPtr,MDB_dbi db,char* key,uint64_t value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    MDB_val mkey;
    mkey.mv_data = key;
    mkey.mv_size = strlen(key);

    MDB_val mvalue;
    mvalue.mv_data = &value;
    mvalue.mv_size = sizeof(value);

    lmdbPut(envPtr,db,mkey,mvalue);

    return 0;
}

int lmdbGet(MDB_env* envPtr,MDB_dbi db,MDB_val* key,MDB_val* value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    assert(key!=nullptr);
    assert(value!=nullptr);

    MDB_txn *txn = nullptr;
    int ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);

    ret = mdb_get(txn,db,key,value);
    if(ret == MDB_NOTFOUND){
        value->mv_data = NULL;
        value->mv_size = 0;
    }else if(ret!=0){
        lmdbAssertIsZero(ret);
    }

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    return 0;
}

int lmdbGetStringInt(MDB_env* envPtr,MDB_dbi db,char* key,int* value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    assert(key!=nullptr);
    assert(value!=nullptr);
    MDB_val k;
    k.mv_data = key;
    k.mv_size = strlen(key);
    MDB_val val;
    lmdbGet(envPtr,db,&k,&val);
    //assert(val.mv_data != NULL);
    //assert(val.mv_size == sizeof(int));
    if(val.mv_data != NULL){
        *value = *static_cast<int*>(val.mv_data);
    }
    return 0;
}

int lmdbGetStringUint64(MDB_env* envPtr,MDB_dbi db,char* key,uint64_t* value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    assert(key!=nullptr);
    assert(value!=nullptr);
    MDB_val k;
    k.mv_data = key;
    k.mv_size = strlen(key);
    MDB_val val;
    lmdbGet(envPtr,db,&k,&val);
    //assert(val.mv_data != NULL);
    //assert(val.mv_size == sizeof(uint64_t));
    if(val.mv_data != NULL){
        *value = *static_cast<uint64_t*>(val.mv_data);
    }
    return 0;
}

int lmdbDeleteAtBegin(MDB_env* envPtr,MDB_dbi db,MDB_val* key,MDB_val* value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    assert(key!=nullptr);
    assert(value!=nullptr);

    MDB_cursor * curs = nullptr;
    MDB_txn * txn = nullptr;
    int ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);

    ret = mdb_cursor_open(txn,db,&curs);
    lmdbAssertIsZero(ret);

    ret = mdb_cursor_get(curs,key,value,MDB_FIRST);
    if(ret == MDB_NOTFOUND){
        mdb_cursor_close(curs);

        ret = mdb_txn_commit(txn);
        lmdbAssertIsZero(ret);
        return 0;
    }else if(ret != 0){
        lmdbAssertIsZero(ret);
    }
    
    ret = mdb_del(txn,db,key,value);
    lmdbAssertIsZero(ret);

    mdb_cursor_close(curs);

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    return 0;
}

int lmdbDeleteAtEnd(MDB_env* envPtr,MDB_dbi db,MDB_val* key,MDB_val* value){
    assert(envPtr!=nullptr);
    assert(db != UINT_MAX);
    assert(key!=nullptr);
    assert(value!=nullptr);

    MDB_cursor * curs = nullptr;
    MDB_txn * txn = nullptr;
    int ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);

    ret = mdb_cursor_open(txn,db,&curs);
    lmdbAssertIsZero(ret);

    ret = mdb_cursor_get(curs,key,value,MDB_LAST);
    if(ret == MDB_NOTFOUND){
        mdb_cursor_close(curs);

        ret = mdb_txn_commit(txn);
        lmdbAssertIsZero(ret);
        return 0;
    }else if(ret != 0){
        lmdbAssertIsZero(ret);
    }
    
    ret = mdb_del(txn,db,key,value);
    lmdbAssertIsZero(ret);

    mdb_cursor_close(curs);

    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    return 0;
}

void lmdbDeleteDb(MDB_env* envPtr,MDB_dbi* dbArray,int dbCount){
    assert(envPtr!=nullptr);
    MDB_txn *txn=nullptr;
    int ret = mdb_txn_begin(envPtr,NULL,0,&txn);
    lmdbAssertIsZero(ret);
    for(int i=0;i<dbCount;++i){
        assert(dbArray[i] != UINT_MAX);
        ret = mdb_drop(txn,dbArray[i],1);
        lmdbAssertIsZero(ret);
    }
    ret = mdb_txn_commit(txn);
    lmdbAssertIsZero(ret);

    for(int i=0;i<dbCount;++i){
        mdb_close(envPtr,dbArray[i]);
    }

    mdb_env_close(envPtr);    
}

static int removeFile(const char* pathName,const struct stat *st,int type,struct FTW *ftw){
    fprintf(stderr,"remove file %s ----- \n",pathName);
    if(remove(pathName) < 0){
        fprintf(stderr,"remove file %s failed \n",pathName);
        return -1;
    }
    return 0;
}

static void deleteDir(const char* path){
    if(nftw(path,removeFile,10,FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0){
        fprintf(stderr,"delete directory %s failed. \n",path);
        assert(0);
    }
}

void lmdbDeleteEnv(MDB_env*,const char* path){
    assert(path != nullptr);

    #if 0
    std::string cmd = std::string("rm -rf ") + std::string(path);
    system(cmd.c_str());
    int s;
    pid_t child = wait(&s);
    //assert(child != -1);
    fprintf(stderr,"!!!delete lmdb path %s",path);
    #else
    deleteDir(path);
    #endif
}
}