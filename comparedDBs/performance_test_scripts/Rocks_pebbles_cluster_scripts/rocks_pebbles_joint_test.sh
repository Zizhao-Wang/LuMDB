#!/bin/bash

# 这是一个用于运行 RocksDB 和 LevelDB 联合测试的自动化脚本
# 它可以通过嵌套 for 循环，针对不同的 Zipfian 参数和阈值进行多次测试，
# 并将每次测试的完整输出记录到独立的日志文件中。
sudo bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'

# 定义可执行文件的路径
EXECUTABLE=./build/rocks_pebbles_joint_test

# 检查可执行文件是否存在
if [ ! -f "$EXECUTABLE" ]; then
    echo "Executable not found at $EXECUTABLE"
    echo "Please run build.sh first."
    exit 1
fi

# --- 定义基础测试参数 ---
# 这些参数在所有循环中保持不变
NUM_KEYS=100000000
VALUE_SIZE=128
BATCH_SIZE=1
NUM_READS=1000000


declare -A ROCKSDB1_MEMTABLE_MAP
declare -A ROCKSDB2_MEMTABLE_MAP

# --- 你可以在这里填充具体的值 ---
# 总 Memtable 大小为 64MB
TOTAL_MEMTABLE_SIZE=$((64 * 1024 * 1024))

# --- 定义循环测试的参数列表 ---
# 在这里定义你想要测试的所有 Zipfian 参数和 Threshold
Cluster_numS=(40 30)
THRESHOLDS=(18446744073709551615) 

# --- 创建日志目录 ---
# 使用日期和时间创建一个唯一的日志目录
LOG_DIR="Rocks_Level_test_dir"
mkdir -p ${LOG_DIR}
echo "Log files will be saved in: ${LOG_DIR}"
echo "======================================================"


# --- 使用嵌套 for 循环进行多次测试 ---
for Cluster_num in "${Cluster_numS[@]}"
do
    for threshold_val in "${THRESHOLDS[@]}"
    do
        # --- 根据循环变量，动态生成本次测试的路径 ---
        # 1. 生成数据文件路径
        DATA_FILE="/mnt/nvm/second_cluster${Cluster_num}.sort"
        
        # 2. 为本次测试生成唯一的数据库路径，包含 Cluster 和 threshold 信息
        ROCKSDB_PATH="/mnt/db_test2/rocks10B/rocksdb_Cluster_${Cluster_num}_thresh_${threshold_val}"
        LEVELDB_PATH="/mnt/db_test2/pebbles10B/leveldb_Cluster_${Cluster_num}_thresh_${threshold_val}"

        # 3. 为本次测试生成唯一的日志文件路径
        LOG_FILE="${LOG_DIR}/Cluster_${Cluster_num}_thresh_${threshold_val}.log"

        # 检查数据文件是否存在
        if [ ! -f "$DATA_FILE" ]; then
            echo "Warning: Data file not found for Cluster=${Cluster_num} at path: ${DATA_FILE}"
            echo "Skipping this iteration."
            continue # 跳过本次循环，继续下一次
        fi
        if [ -f "$LOG_FILE" ]; then
            echo "Log file already exists for Cluster=${Cluster_num}, threshold=${threshold_val}: ${LOG_FILE}"
            echo "Skipping this iteration."
            continue
        fi 

        # --- 在循环开始时检查并清理磁盘 ---
        ROCKSDB_PATH="/mnt/db_test2/rocks10B/"
        LEVELDB_PATH="/mnt/db_test2/pebbles10B/"
        REQUIRED_FREE_SPACE_GB=20
        THRESHOLD_KB=$((REQUIRED_FREE_SPACE_GB * 1024 * 1024))  # 转换为KB

        # 使用磁盘挂载点来检查空间（取两个路径的公共挂载点）
        DISK_MOUNT_POINT="/mnt/db_test2"

        # 2. 获取磁盘可用空间 (单位: KB)
        available_space_kb=$(df --output=avail "${DISK_MOUNT_POINT}/" | tail -n 1)
        echo "--------------------------------------------------"
        echo "Checking disk space on ${DISK_MOUNT_POINT}/"
        echo "Available space: $((available_space_kb / 1024 / 1024)) GB. Required: ${REQUIRED_FREE_SPACE_GB} GB."
        echo "Cleanup targets: ${ROCKSDB_PATH} and ${LEVELDB_PATH}"

        # 3. 判断空间是否不足
        if [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; then
            echo "Low disk space detected. Available space is less than ${REQUIRED_FREE_SPACE_GB}GB."
            echo "Starting cleanup of largest files in both directories..."
            
            # 4. 循环删除最大的文件，直到空间足够
            while [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; do
                # 在两个目录中找到最大的文件
                largest_file=""
                largest_size=0
                
                # 检查 ROCKSDB_PATH 目录
                if [ -d "$ROCKSDB_PATH" ]; then
                    rocksdb_largest=$(find "${ROCKSDB_PATH}/" -type f -exec ls -la {} + 2>/dev/null | \
                                    awk '{print $5 " " $9}' | \
                                    sort -nr | \
                                    head -1)
                    if [ -n "$rocksdb_largest" ]; then
                        rocksdb_size=$(echo "$rocksdb_largest" | awk '{print $1}')
                        rocksdb_file=$(echo "$rocksdb_largest" | awk '{print $2}')
                        if [ "$rocksdb_size" -gt "$largest_size" ]; then
                            largest_size="$rocksdb_size"
                            largest_file="$rocksdb_file"
                        fi
                    fi
                fi
                
                # 检查 LEVELDB_PATH 目录
                if [ -d "$LEVELDB_PATH" ]; then
                    leveldb_largest=$(find "${LEVELDB_PATH}/" -type f -exec ls -la {} + 2>/dev/null | \
                                    awk '{print $5 " " $9}' | \
                                    sort -nr | \
                                    head -1)
                    if [ -n "$leveldb_largest" ]; then
                        leveldb_size=$(echo "$leveldb_largest" | awk '{print $1}')
                        leveldb_file=$(echo "$leveldb_largest" | awk '{print $2}')
                        if [ "$leveldb_size" -gt "$largest_size" ]; then
                            largest_size="$leveldb_size"
                            largest_file="$leveldb_file"
                        fi
                    fi
                fi
                
                # 如果没有找到文件，退出循环
                if [ -z "$largest_file" ] || [ ! -f "$largest_file" ] || [ "$largest_size" -eq 0 ]; then
                    echo "No more files to delete in either directory"
                    break
                fi
                
                # 获取文件大小（以MB为单位显示）
                file_size_mb=$((largest_size / 1024 / 1024))
                
                # 判断文件属于哪个目录
                if [[ "$largest_file" == *"rocksdb_"* ]]; then
                    echo "Deleting largest file from RocksDB: $largest_file (Size: ${file_size_mb} MB)"
                else
                    echo "Deleting largest file from LevelDB: $largest_file (Size: ${file_size_mb} MB)"
                fi
                
                # 删除文件
                rm -f "$largest_file"
                
                # 重新检查磁盘空间
                available_space_kb=$(df --output=avail "${DISK_MOUNT_POINT}/" | tail -n 1)
                echo "After deletion - Available space: $((available_space_kb / 1024 / 1024)) GB"
                
                # 如果还需要继续清理，显示进度
                if [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; then
                    echo "Continue cleanup... Current available: $((available_space_kb / 1024 / 1024)) GB, Target: ${REQUIRED_FREE_SPACE_GB} GB"
                fi
            done
            
            echo "Cleanup finished. Final available space: $((available_space_kb / 1024 / 1024)) GB"
        else
            echo "Sufficient disk space available."
        fi
        # --- 磁盘检查与清理逻辑结束 ---

        # --- 执行并记录日志 ---
        # 使用子 shell 和 tee 命令，将所有输出 (包括标准输出和标准错误) 
        # 同时显示在屏幕上并写入到日志文件中。
        (
            # 清理本次测试可能残留的旧数据库文件
            echo "Cleaning up old database directories for Cluster=${Cluster_num}, threshold=${threshold_val}..."
            rm -rf ${ROCKSDB_PATH}
            rm -rf ${LEVELDB_PATH}

            # 确保父目录存在
            mkdir -p $(dirname ${ROCKSDB_PATH})
            mkdir -p $(dirname ${LEVELDB_PATH})

            echo "======================================================"
            echo "  STARTING TEST FOR Cluster = ${Cluster_num}, THRESHOLD = ${threshold_val}"
            echo "------------------------------------------------------"
            echo "  - Log File:     ${LOG_FILE}"
            echo "  - Data File:    ${DATA_FILE}"
            echo "  - RocksDB Path: ${ROCKSDB_PATH}"
            echo "  - LevelDB Path: ${LEVELDB_PATH}"
            echo "  - Total Keys:   ${NUM_KEYS}"
            echo "  - Value Size:   ${VALUE_SIZE} bytes"
            echo "  - Threshold:    ${threshold_val}"
            echo "======================================================"

            # 执行测试，传递所有参数
            $EXECUTABLE \
                --num_keys=${NUM_KEYS} \
                --value_size=${VALUE_SIZE} \
                --threshold=${threshold_val} \
                --batch_size=${BATCH_SIZE} \
                --num_reads=${NUM_READS} \
                --rocksdb_path=${ROCKSDB_PATH} \
                --leveldb_path=${LEVELDB_PATH} \
                --data_file=${DATA_FILE}
        ) 2>&1 | tee "${LOG_FILE}"

        # 在两次测试之间添加一些空行，使输出更清晰
        echo -e "\n\n"

    done
done

echo "All test runs finished. Check the '${LOG_DIR}' directory for detailed logs."
