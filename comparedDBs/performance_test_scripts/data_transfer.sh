#!/bin/bash

# 定义变量
SOURCE_DIR=~/workloads
TARGET_DIR=/mnt/workloads
TARGET_USER=root  # 替换为接收机器的用户名
TARGET_IP=172.20.111.156
TARGET_PORT=501  # 使用的SSH端口号

# 传输文件到目标机器的目标目录
scp -P ${TARGET_PORT} ${SOURCE_DIR}/zipf1.{2,3,4,5}* ${TARGET_USER}@${TARGET_IP}:${TARGET_DIR}
