cmake_minimum_required(VERSION 3.0.0)
project(lmdb VERSION 0.1.0)

add_library(lmdb mdb.c midl.c)
set_property(TARGET lmdb PROPERTY C_STANDARD 99)
target_link_libraries(lmdb PUBLIC pthread)