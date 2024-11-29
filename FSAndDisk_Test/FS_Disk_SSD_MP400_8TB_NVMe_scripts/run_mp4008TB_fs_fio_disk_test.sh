#!/bin/bash

# 打印开始信息
echo "Starting all fio tests..."

# Check if the script is run as root
if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root. Exiting..."
    exit 1
fi

MOUNTPOINT="/mnt/mp4008TB_disk_test"
FILENAME="${MOUNTPOINT}/filetest.txt"
export FILENAME

DEVICENAME="nvme0n1"
export DEVICENAME
MOUNTDEVICE="/dev/${DEVICENAME}"

# Check if the mount point directory exists; if not, create it
if [ ! -d "$MOUNTPOINT" ]; then
    echo "Directory $MOUNTPOINT does not exist, creating it..."
    mkdir -p "$MOUNTPOINT"
fi

# Check if the device has been formatted as ext4
FS_TYPE=$(blkid -o value -s TYPE "$MOUNTDEVICE")
if [ "$FS_TYPE" != "ext4" ]; then
    echo "Device $MOUNTDEVICE is not formatted as ext4, formatting now..."
    mkfs.ext4 "$MOUNTDEVICE"
fi

# Check if the device is mounted to the specified mount point
if ! grep -qs "$MOUNTPOINT" /proc/mounts; then
    echo "Device $MOUNTDEVICE is not mounted to $MOUNTPOINT, mounting now..."
    mount "$MOUNTDEVICE" "$MOUNTPOINT"
fi

# Check if the file exists
if [ ! -f "$FILENAME" ]; then
    echo "File $FILENAME does not exist, creating it..."
    touch "$FILENAME"
fi

echo "All setups complete, system is ready!"


IOSTAT_INTERVAL=1
export IOSTAT_INTERVAL

SIZE=214748364800
export SIZE

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
echo "Running write test script..."
./mp4008TB_fs_fio_write_test_script.sh


# 运行读测试脚本
echo "Running read test script..."
./mp4008TB_fs_fio_read_test_script.sh


# 打印完成信息
echo "All tests completed."
