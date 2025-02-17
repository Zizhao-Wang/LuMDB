#!/bin/bash

# 定义变量
TARGET_DIR=/mnt/workloads
TARGET_FILE=zipf1.5_keys10.0B.csv
LOCAL_SCRIPT=./hotdb_script_zipf_removal_hot_SATASSD.sh
CHECK_INTERVAL=600  # 检查间隔时间（秒），10分钟为600秒

# 定义函数检查目标文件是否存在
check_file_existence() {
  if [ -f "${TARGET_DIR}/${TARGET_FILE}" ]; then
    echo "File ${TARGET_DIR}/${TARGET_FILE} exists, waiting for 30 minutes..."
    sleep 1800  # 等待30分钟（1800秒）
    echo "Starting script ${LOCAL_SCRIPT}"
    bash ${LOCAL_SCRIPT}
    exit 0
  else
    echo "File ${TARGET_DIR}/${TARGET_FILE} does not exist, continuing to monitor..."
  fi
}

# 监控循环
while true; do
  check_file_existence
  sleep ${CHECK_INTERVAL}
done
