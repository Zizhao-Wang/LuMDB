#!/bin/bash

# 打印开始信息
echo "Starting all fio tests..."

FILENAME="/dev/nvme0n1"
export FILENAME

# 运行写测试脚本
# echo "Running write test script..."
# ./mp4008TB_fio_write_test_script.sh

# # 运行读写测试脚本
# echo "Running read-write test script..."
# ./mp4008TB_fio_read_write_script.sh

# # 运行读测试脚本
# echo "Running read test script..."
# ./mp4008TB_fio_read_test_script.sh

# 运行读测试脚本-多队列
echo "Running read test script with multi-queues..."
./mp4008TB_fio_read_test_multi_queue_scripts.sh

# 打印完成信息
echo "All tests completed."
