cmake_minimum_required(VERSION 3.0.0)
project(craft VERSION 0.1.0)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0" CACHE STRING "append c debug flags" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0" CACHE STRING "append cxx debug flags" FORCE)

include(CTest)
enable_testing()

add_subdirectory(thirdparty/sds)
add_subdirectory(thirdparty/simple_logger)
add_subdirectory(thirdparty/lmdb)

add_library(craft src/raft.c)
set_property(TARGET craft PROPERTY C_STANDARD 99)
target_link_libraries(craft PUBLIC sds)
target_include_directories(craft PUBLIC "${PROJECT_SOURCE_DIR}/thirdparty/sds")

add_executable(basicsimulator tests/basicsimulator.cpp tests/simulator.cpp tests/lmdbwrapper.cpp)
target_link_libraries(basicsimulator PUBLIC craft)
target_link_libraries(basicsimulator PUBLIC gflags)
target_link_libraries(basicsimulator PUBLIC simple_logger)
target_link_libraries(basicsimulator PUBLIC lmdb)
target_include_directories(basicsimulator PUBLIC 
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/thirdparty/simple_logger/src"
    "${PROJECT_SOURCE_DIR}/thirdparty/lmdb"
    )

add_executable(testmain tests/testmain.cpp tests/simulator.cpp tests/lmdbwrapper.cpp)
target_link_libraries(testmain PUBLIC craft)
target_link_libraries(testmain PUBLIC gflags)
target_link_libraries(testmain PUBLIC simple_logger)
target_link_libraries(testmain PUBLIC lmdb)
target_include_directories(testmain PUBLIC 
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/thirdparty/simple_logger/src"
    "${PROJECT_SOURCE_DIR}/thirdparty/lmdb"
    )


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
