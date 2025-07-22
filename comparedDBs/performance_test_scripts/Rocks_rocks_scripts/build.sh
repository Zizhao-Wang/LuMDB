#!/bin/bash

# 这是一个用于编译双 RocksDB 联合测试项目的脚本
# 它会先清理旧的构建目录，然后进行全新的编译

# 设置 RocksDB 的根目录
# **重要**: 请将下面的路径修改为你的实际路径
export ROCKSDB_DIR=/home/jeff-wang/LuMDB/comparedDBs/rocksdb

# 检查依赖目录是否存在
if [ ! -d "$ROCKSDB_DIR" ]; then
    echo "Error: RocksDB directory not found at $ROCKSDB_DIR"
    echo "Please edit build.sh to set the correct ROCKSDB_DIR."
    exit 1
fi

# 验证 RocksDB 是否已经编译
if [ ! -f "$ROCKSDB_DIR/librocksdb.a" ] && [ ! -f "$ROCKSDB_DIR/librocksdb.so" ]; then
    echo "Warning: RocksDB library not found. Make sure RocksDB is compiled."
    echo "You may need to run 'make static_lib' or 'make shared_lib' in $ROCKSDB_DIR"
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
# **重要**: 确保你的 CMakeLists.txt 是为双 RocksDB 配置的版本
# 这个命令会把 ROCKSDB_DIR 的值传递给 CMake
echo "Configuring project with CMake..."
cmake .. -DROCKSDB_DIR=$ROCKSDB_DIR

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
echo "Expected executable: build/rocks_rocks_joint_test"