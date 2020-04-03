#ifndef __LMDB_WRAPPER_H__
#define __LMDB_WRAPPER_H__

#include "lmdb.h"

namespace Lmdb{

#define lmdbAssertIsZero(e) do{ \
        if((e) != 0){ \
            fprintf(stderr, "%s:%d - err:%d: %s\n", __FILE__, __LINE__, e, mdb_strerror((e))); \
            assert((e) == 0);\
        } \
    }while(0)

/**
 * 在指定的目录中，创建一个lmdb环境句柄
 */
void createLmdbEnv(MDB_env**,unsigned int,const char*,int);

/**
 * 在 lmdb环境中创建一个数据库
 * 
 */
void createLmdbDb(MDB_env*,MDB_dbi*,const char*,int);

/**
 * 在lmdb数据库中存储健值<key,value>，包裹在事务中
 * 返回0 表示成功
 */
int lmdbPut(MDB_env*,MDB_dbi,MDB_val,MDB_val);

/**
 * 在lmdb数据库中存储健值<字符串,int>，包裹在事务中
 */
int lmdbPutStringInt(MDB_env*,MDB_dbi,char*,int);

/**
 * 在lmdb数据库中存储健值<字符串,uint64_t>，包裹在事务中
 */
int lmdbPutStringUint64(MDB_env*,MDB_dbi,char*,uint64_t);

/**
 * 在lmdb数据库中查找健key,取value，包裹在事务中
 * 返回0 表示成功
 */
int lmdbGet(MDB_env*,MDB_dbi,MDB_val*,MDB_val*);

/**
 * 在lmdb数据库中查找健字符串,取int，包裹在事务中
 */
int lmdbGetStringInt(MDB_env*,MDB_dbi,char*,int*);

/**
 * 在lmdb数据库中查找健字符串,取uint64，包裹在事务中
 */
int lmdbGetStringUint64(MDB_env*,MDB_dbi,char*,uint64_t*);

/**
 * 弹出lmdb数据库中的第一个数据，包裹在事务中
 */
int lmdbDeleteAtBegin(MDB_env*,MDB_dbi,MDB_val*,MDB_val*);

/**
 * 弹出lmdb数据库中的最后一个数据，包裹在事务中
 */
int lmdbDeleteAtEnd(MDB_env*,MDB_dbi,MDB_val*,MDB_val*);

/**
 * lmdb删除数据库，包裹在事务中
 */
void lmdbDeleteDb(MDB_env*,MDB_dbi*,int);

/**
 * lmdb删除数据库环境，以及其工作目录
 */
void lmdbDeleteEnv(MDB_env*,const char*);
}

#endif