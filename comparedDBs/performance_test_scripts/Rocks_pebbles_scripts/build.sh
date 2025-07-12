#!/bin/bash

# 这是一个用于编译项目的脚本
# 它会先清理旧的构建目录，然后进行全新的编译

# 设置 RocksDB 和 PebblesDB 的根目录
# **重要**: 请将下面的路径修改为你的实际路径
export ROCKSDB_DIR=/home/jeff-wang/LuMDB/comparedDBs/rocksdb
export PEBBLESDB_DIR=/home/jeff-wang/LuMDB/comparedDBs/pebblesdb

# 检查依赖目录是否存在
if [ ! -d "$ROCKSDB_DIR" ]; then
    echo "Error: RocksDB directory not found at $ROCKSDB_DIR"
    echo "Please edit build.sh to set the correct ROCKSDB_DIR."
    exit 1
fi

if [ ! -d "$PEBBLESDB_DIR" ]; then
    echo "Error: PebblesDB directory not found at $PEBBLESDB_DIR"
    echo "Please edit build.sh to set the correct PEBBLESDB_DIR."
    exit 1
fi

# --- 新增的清理步骤 ---
# 如果 build 目录存在，先删除它
if [ -d "build" ]; then
    echo "Removing existing build directory..."
    rm -rf build
fi

# 创建一个全新的构建目录
echo "Creating a new build directory..."
mkdir -p build
cd build

# 运行 CMake 来配置项目
# CMake 会从 CMakeLists.txt 中读取 ROCKSDB_DIR 和 PEBBLESDB_DIR 的设置
cmake .. -DROCKSDB_DIR=$ROCKSDB_DIR -DPEBBLESDB_DIR=$PEBBLESDB_DIR

# 检查 CMake 是否成功
if [ $? -ne 0 ]; then
    echo "CMake configuration failed."
    exit 1
fi

# 使用 make 进行编译 (使用所有可用的 CPU核心 加速编译)
echo "Building the project..."
make -j$(nproc)

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo "Build failed."
    exit 1
fi

echo "Build successful! The executable is in the build/ directory."
