cmake_minimum_required(VERSION 3.10)
project(DistributedKVStore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

# Ensure consistent_hash.cpp is in this list!
add_executable(kvstore 
    main.cpp 
    server.cpp 
    thread_pool.cpp 
    wal.cpp 
    raft.cpp 
    consistent_hash.cpp
)

target_link_libraries(kvstore Threads::Threads)