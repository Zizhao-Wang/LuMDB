# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(DualRocksDBJointTest CXX)

set(CMAKE_BUILD_TYPE Debug)
message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 查找依赖项 ---

# 查找线程库
find_package(Threads REQUIRED)

# 查找 gflags
find_package(gflags REQUIRED)
if(NOT gflags_FOUND)
    message(FATAL_ERROR "gflags not found. Please install it (e.g., sudo apt-get install libgflags-dev) or set its path manually.")
endif()

# 查找 RocksDB
# **重要**: 你需要将下面的路径替换为你的实际安装路径
set(ROCKSDB_DIR "/home/jeff-wang/LuMDB/comparedDBs/rocksdb" CACHE PATH "Path to RocksDB installation")

# 验证 RocksDB 路径是否存在
if(NOT EXISTS "${ROCKSDB_DIR}/include")
    message(FATAL_ERROR "RocksDB include directory not found at ${ROCKSDB_DIR}/include")
endif()

# 设置头文件搜索路径
include_directories(
    ${gflags_INCLUDE_DIRS}
    "${ROCKSDB_DIR}/include"
)

# 设置库文件搜索路径
link_directories(
    "${ROCKSDB_DIR}/release"
    "${ROCKSDB_DIR}"  # 备用库文件路径
)

# 添加可执行文件
add_executable(rocks_rocks_joint_test rocks_rocks_joint_test.cpp)

# 链接所有需要的库
target_link_libraries(rocks_rocks_joint_test
    PRIVATE
    Threads::Threads
    ${gflags_LIBRARIES}
    rocksdb
    snappy
    z
    bz2
    lz4
    zstd
)

# 打印信息以便调试
message(STATUS "RocksDB include path: ${ROCKSDB_DIR}/include")
message(STATUS "RocksDB library path: ${ROCKSDB_DIR}/release")
message(STATUS "gflags include path: ${gflags_INCLUDE_DIRS}")
message(STATUS "gflags libraries: ${gflags_LIBRARIES}")
message(STATUS "Target executable: rocks_rocks_joint_test")