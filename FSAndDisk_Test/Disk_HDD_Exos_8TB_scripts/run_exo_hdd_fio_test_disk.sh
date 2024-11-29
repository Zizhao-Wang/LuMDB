#!/bin/bash

# 打印开始信息
echo "Starting all fio tests..."

FILENAME="/dev/sdb"
export FILENAME


append_device_info() {
    local output_file="$1"
    local file_name="$2"

    # 获取当前时间
    local current_time=$(date "+%Y-%m-%d %H:%M:%S")

    # 获取设备名称
    local device_name=$(basename "$file_name")

    # 获取设备信息
    local device_info=$(lsblk -no NAME,SIZE,MODEL "$file_name")

    # 将时间、设备名和设备信息附加到输出文件
    echo -e "\nTest Time: $current_time\nDevice Name: $device_name\nDevice Info: $device_info" | sudo tee -a "$output_file"
}
# 导出函数以便子脚本可以使用
export -f append_device_info

# 运行写测试脚本
# echo "Running write test script..."
# ./exo_hdd_fio_write_test_script.sh
# 2024.5.2 15:40 wrirte operations have been finished!


# 运行读写测试脚本
# echo "Running read-write test script..."
# ./exo_hdd_fio_read_write_script.sh

# # 运行读测试脚本
# echo "Running read test script..."
# ./exo_hdd_fio_read_test_script.sh

# 运行读多队列测试脚本
echo "Running read with multi-queue script..."
./exo_hdd_fio_read_multiqueue_scripts.sh

# 打印完成信息
echo "All tests completed."
