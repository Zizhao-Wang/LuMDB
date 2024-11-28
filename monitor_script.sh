#!/bin/bash

# 要监控的磁盘列表
DISK_LIST=("nvme0n1")

# 获取初始的读写统计信息
declare -A prev_read_sectors
declare -A prev_write_sectors

# 初始化磁盘的读取和写入扇区
for DISK in "${DISK_LIST[@]}"; do
    prev_read_sectors["$DISK"]=0
    prev_write_sectors["$DISK"]=0
done

# 设置循环时间间隔（例如 1 秒）
while true; do
    for DISK in "${DISK_LIST[@]}"; do
        # 获取当前的磁盘统计信息
        stats=$(cat /proc/diskstats | grep "$DISK")
        
        # 提取读取和写入的扇区数
        read_sectors=$(echo $stats | awk '{print $6}')
        write_sectors=$(echo $stats | awk '{print $10}')
        
        # 计算读写的差异（单位：字节），并换算为 GB
        read_diff=$((read_sectors - prev_read_sectors["$DISK"]))
        write_diff=$((write_sectors - prev_write_sectors["$DISK"]))

        # 计算每秒的读写字节量
        read_bytes=$((read_diff * 512))  # 1 扇区 = 512 字节
        write_bytes=$((write_diff * 512))

        # 打印结果（可以根据需求转换单位为 MB 或 GB）
        echo "Disk $DISK: Read: $((read_bytes / 1024 / 1024)) MB | Write: $((write_bytes / 1024 / 1024)) MB"

        # 更新先前的读写扇区数
        prev_read_sectors["$DISK"]=$read_sectors
        prev_write_sectors["$DISK"]=$write_sectors
    done

    # 等待一秒钟
    sleep 1
done
