#!/bin/bash

# 打印开始信息
echo "Starting all fio tests..."

FILENAME="/dev/nvme1n1"
export FILENAME

IOSTAT_INTERVAL=1
export IOSTAT_INTERVAL

# # 运行写测试脚本
# echo "Running write test script..."
# ./mp4004TB_fio_write_test_script.sh

# # 运行读写测试脚本
# echo "Running read-write test script..."
# ./mp4004TB_fio_read_write_script.sh

# # 运行读测试脚本
# echo "Running read test script..."
# ./mp4004TB_fio_read_test_script.sh


# 运行读测试脚本
echo "Running read with multi-queue script..."
./mp4004TB_disk_fio_read_multiqueue_scripts.sh

# 打印完成信息
echo "All tests completed."
