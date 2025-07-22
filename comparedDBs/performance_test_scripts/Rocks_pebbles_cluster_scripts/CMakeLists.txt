# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(RocksPebblesJointTest CXX)

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

# 查找 RocksDB 和 PebblesDB
set(ROCKSDB_DIR "/home/jeff-wang/LuMDB/comparedDBs/rocksdb" CACHE PATH "Path to RocksDB installation")
set(PEBBLESDB_DIR "/home/jeff-wang/LuMDB/comparedDBs/pebblesdb" CACHE PATH "Path to PebblesDB installation")

# --- 解决 PebblesDB 头文件路径问题 (改进版本) ---
# 创建虚拟 include 目录
set(PEBBLESDB_VIRTUAL_INCLUDE_DIR ${CMAKE_BINARY_DIR}/pebblesdb_virtual_include)

# 检查可能的头文件路径
set(PEBBLESDB_POSSIBLE_PATHS
    "${PEBBLESDB_DIR}/src/include"
    "${PEBBLESDB_DIR}/include"
    "${PEBBLESDB_DIR}/src"
    "${PEBBLESDB_DIR}"
)

set(PEBBLESDB_REAL_INCLUDE_PATH "")
foreach(path ${PEBBLESDB_POSSIBLE_PATHS})
    if(EXISTS "${path}/db.h" OR EXISTS "${path}/pebblesdb/db.h")
        set(PEBBLESDB_REAL_INCLUDE_PATH ${path})
        message(STATUS "Found PebblesDB headers at: ${path}")
        break()
    endif()
endforeach()

if(NOT PEBBLESDB_REAL_INCLUDE_PATH)
    message(FATAL_ERROR "Could not find PebblesDB headers. Checked paths: ${PEBBLESDB_POSSIBLE_PATHS}")
endif()

# 创建虚拟目录和符号链接
file(MAKE_DIRECTORY ${PEBBLESDB_VIRTUAL_INCLUDE_DIR})
file(MAKE_DIRECTORY ${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb)

# 使用 configure_file 或 execute_process 来创建符号链接
# 如果符号链接失败，尝试直接复制头文件
if(EXISTS "${PEBBLESDB_REAL_INCLUDE_PATH}/db.h")
    # 情况1: db.h 直接在 include 目录下
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink 
        "${PEBBLESDB_REAL_INCLUDE_PATH}/db.h" 
        "${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb/db.h"
        RESULT_VARIABLE SYMLINK_RESULT
    )
    if(NOT SYMLINK_RESULT EQUAL 0)
        # 符号链接失败，尝试复制文件
        file(COPY "${PEBBLESDB_REAL_INCLUDE_PATH}/db.h" 
             DESTINATION "${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb/")
        message(WARNING "Symbolic link failed, copied file instead")
    endif()
elseif(EXISTS "${PEBBLESDB_REAL_INCLUDE_PATH}/pebblesdb/db.h")
    # 情况2: db.h 在 pebblesdb 子目录下
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink 
        "${PEBBLESDB_REAL_INCLUDE_PATH}/pebblesdb" 
        "${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb"
        RESULT_VARIABLE SYMLINK_RESULT
    )
    if(NOT SYMLINK_RESULT EQUAL 0)
        # 符号链接失败，尝试复制目录
        file(COPY "${PEBBLESDB_REAL_INCLUDE_PATH}/pebblesdb/" 
             DESTINATION "${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb/")
        message(WARNING "Symbolic link failed, copied directory instead")
    endif()
else()
    message(FATAL_ERROR "Could not find db.h in expected locations")
endif()

# 设置头文件搜索路径
include_directories(
    ${gflags_INCLUDE_DIRS}
    "${ROCKSDB_DIR}/include"
    ${PEBBLESDB_VIRTUAL_INCLUDE_DIR}
    ${PEBBLESDB_REAL_INCLUDE_PATH}  # 添加原始路径作为备选
)

# 设置库文件搜索路径
# 检查可能的库路径
set(PEBBLESDB_POSSIBLE_LIB_PATHS
    "${PEBBLESDB_DIR}/release"
    "${PEBBLESDB_DIR}/build"
    "${PEBBLESDB_DIR}/lib"
    "${PEBBLESDB_DIR}"
)

set(PEBBLESDB_LIB_PATH "")
foreach(path ${PEBBLESDB_POSSIBLE_LIB_PATHS})
    if(EXISTS "${path}/libpebblesdb.a" OR EXISTS "${path}/libpebblesdb.so")
        set(PEBBLESDB_LIB_PATH ${path})
        message(STATUS "Found PebblesDB library at: ${path}")
        break()
    endif()
endforeach()

if(NOT PEBBLESDB_LIB_PATH)
    message(WARNING "Could not find PebblesDB library. Will try default paths.")
    set(PEBBLESDB_LIB_PATH "${PEBBLESDB_DIR}/release")
endif()

link_directories(
    "${ROCKSDB_DIR}/release"
    ${PEBBLESDB_LIB_PATH}
)

# 添加可执行文件
add_executable(rocks_pebbles_joint_test rocks_pebbles_joint_test.cpp)

# 链接所有需要的库
target_link_libraries(rocks_pebbles_joint_test
    PRIVATE
    Threads::Threads
    ${gflags_LIBRARIES}
    rocksdb
    pebblesdb
    snappy
    z
    bz2
    lz4
    zstd
)

# 打印调试信息
message(STATUS "RocksDB include path: ${ROCKSDB_DIR}/include")
message(STATUS "PebblesDB REAL include path: ${PEBBLESDB_REAL_INCLUDE_PATH}")
message(STATUS "PebblesDB VIRTUAL include path: ${PEBBLESDB_VIRTUAL_INCLUDE_DIR}")
message(STATUS "PebblesDB library path: ${PEBBLESDB_LIB_PATH}")
message(STATUS "gflags include path: ${gflags_INCLUDE_DIRS}")
message(STATUS "gflags libraries: ${gflags_LIBRARIES}")

# 检查关键文件是否存在
if(EXISTS "${PEBBLESDB_VIRTUAL_INCLUDE_DIR}/pebblesdb/db.h")
    message(STATUS "✓ Virtual pebblesdb/db.h exists")
else()
    message(WARNING "✗ Virtual pebblesdb/db.h does not exist")
endif()