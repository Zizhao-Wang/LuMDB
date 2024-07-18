#!/bin/bash

# 定义变量
SOURCE_DIR=~/workloads
TARGET_DIR=/mnt/workloads
TARGET_USER=root  # 替换为接收机器的用户名
TARGET_IP=172.20.111.156
TARGET_PORT=501  # 使用的SSH端口号

# 查找符合模式的文件
FILES=$(find ${SOURCE_DIR} -name "zipf1.1*")

# 检查是否找到文件
if [ -z "$FILES" ]; then
  echo "没有找到匹配的文件"
  exit 1
fi

# 传输文件到目标机器的目标目录
scp -P ${TARGET_PORT} $FILES ${TARGET_USER}@${TARGET_IP}:${TARGET_DIR}
