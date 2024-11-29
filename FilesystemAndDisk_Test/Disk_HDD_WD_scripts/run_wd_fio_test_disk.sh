#!/bin/bash

# 打印开始信息
echo "Starting all fio tests..."


# 运行写测试脚本
echo "Running write test script..."
./wdhdd_fio_write_test_script.sh

# 运行读测试脚本
echo "Running read test script..."
./wdhdd_fio_read_test_script.sh

# 运行读写测试脚本
echo "Running read-write test script..."
./wdhdd_fio_read_write_script.sh



# 打印完成信息
echo "All tests completed."
