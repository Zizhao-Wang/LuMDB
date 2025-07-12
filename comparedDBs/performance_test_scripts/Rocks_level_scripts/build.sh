#!/bin/bash

# 这是一个用于编译 RocksDB 和 LevelDB 联合测试项目的脚本
# 它会先清理旧的构建目录，然后进行全新的编译

# 设置 RocksDB 和 LevelDB 的根目录
# **重要**: 请将下面的路径修改为你的实际路径
export ROCKSDB_DIR=/home/jeff-wang/LuMDB/comparedDBs/rocksdb
export LEVELDB_DIR=/home/jeff-wang/LuMDB/comparedDBs/leveldb # <-- 请将这里修改为你的 LevelDB 路径

# 检查依赖目录是否存在
if [ ! -d "$ROCKSDB_DIR" ]; then
    echo "Error: RocksDB directory not found at $ROCKSDB_DIR"
    echo "Please edit build.sh to set the correct ROCKSDB_DIR."
    exit 1
fi

if [ ! -d "$LEVELDB_DIR" ]; then
    echo "Error: LevelDB directory not found at $LEVELDB_DIR"
    echo "Please edit build.sh to set the correct LEVELDB_DIR."
    exit 1
fi

# --- 清理步骤 ---
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
# **重要**: 确保你的 CMakeLists.txt 是为 LevelDB 配置的版本
# 这个命令会把 ROCKSDB_DIR 和 LEVELDB_DIR 的值传递给 CMake
cmake .. -DROCKSDB_DIR=$ROCKSDB_DIR -DLEVELDB_DIR=$LEVELDB_DIR

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
