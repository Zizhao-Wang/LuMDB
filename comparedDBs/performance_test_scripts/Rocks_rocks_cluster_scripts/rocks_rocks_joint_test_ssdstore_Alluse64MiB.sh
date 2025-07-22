#!/bin/bash  
# 这是一个用于运行双 RocksDB 联合测试的自动化脚本
# 它可以通过嵌套 for 循环,针对不同的 Clusterian 参数和阈值进行多次测试,
# 并将每次测试的完整输出记录到独立的日志文件中。

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'

# 定义可执行文件的路径
EXECUTABLE=./build/rocks_rocks_joint_test

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

# --- 定义 RocksDB1 (热数据) 的默认配置参数 ---
ROCKSDB1_L0_TRIGGER=4
ROCKSDB1_L0_SLOWDOWN=20
ROCKSDB1_L0_STOP=36
ROCKSDB1_MAX_BYTES_BASE=$((256 * 1024 * 1024))
ROCKSDB1_MAX_BYTES_MULT=10.0
ROCKSDB1_BLOCK_CACHE=$((128 * 1024 * 1024))
ROCKSDB1_MAX_JOBS=8

# --- 定义 RocksDB2 (冷数据) 的默认配置参数 ---
ROCKSDB2_L0_TRIGGER=4
ROCKSDB2_L0_SLOWDOWN=20
ROCKSDB2_L0_STOP=36
ROCKSDB2_MAX_BYTES_BASE=$((256 * 1024 * 1024))
ROCKSDB2_MAX_BYTES_MULT=10.0
ROCKSDB2_BLOCK_CACHE=$((128 * 1024 * 1024))
ROCKSDB2_MAX_JOBS=8

# --- **新增**: 定义动态 Memtable 分配 ---
# 使用关联数组 (associative array) 来根据阈值映射不同的 memtable 大小
# 键 (key) 是 THRESHOLD 值, 值 (value) 是分配的 memtable 大小 (单位: 字节)
declare -A ROCKSDB1_MEMTABLE_MAP
declare -A ROCKSDB2_MEMTABLE_MAP

# --- 你可以在这里填充具体的值 ---
# 总 Memtable 大小为 64MB
TOTAL_MEMTABLE_SIZE=$((64 * 1024 * 1024))

# Cluster 40 - threshold 10: 热数据占0.28%
ROCKSDB1_MEMTABLE_MAP["40_0"]=${TOTAL_MEMTABLE_SIZE}
ROCKSDB2_MEMTABLE_MAP["40_0"]=0

# Cluster 40 - threshold 10: 热数据占0.28%
ROCKSDB2_MEMTABLE_MAP["40_10"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0028 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_10"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_10"]}))

# Cluster 40 - threshold 100: 热数据占2.61%
ROCKSDB2_MEMTABLE_MAP["40_100"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0261 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_100"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_100"]}))

# Cluster 40 - threshold 500: 热数据占8.19%
ROCKSDB2_MEMTABLE_MAP["40_500"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0819 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_500"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_500"]}))

# Cluster 40 - threshold 1000: 热数据占11.58%
ROCKSDB2_MEMTABLE_MAP["40_1000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.1158 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_1000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_1000"]}))

# Cluster 40 - threshold 2000: 热数据占15.65%
ROCKSDB2_MEMTABLE_MAP["40_2000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.1565 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_2000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_2000"]}))

# Cluster 40 - threshold 5000: 热数据占21.99%
ROCKSDB2_MEMTABLE_MAP["40_5000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.2199 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_5000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_5000"]}))

# Cluster 40 - threshold 10000: 热数据占27.36%
ROCKSDB2_MEMTABLE_MAP["40_10000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.2736 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_10000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_10000"]}))

# Cluster 40 - threshold 20000: 热数据占33.16%
ROCKSDB2_MEMTABLE_MAP["40_20000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.3316 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_20000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_20000"]}))

# Cluster 40 - threshold 50000: 热数据占41.33%
ROCKSDB2_MEMTABLE_MAP["40_50000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.4133 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_50000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_50000"]}))

# Cluster 40 - threshold 100000: 热数据占47.73%
ROCKSDB2_MEMTABLE_MAP["40_100000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.4773 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["40_100000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["40_100000"]}))


# ========================================
# Cluster 49 的配置 (基于实际数据分析)
# ========================================
# 任务2: 高频Key占比分析 - Cluster 49:
# 前     10 个key:    0.11%
# 前    100 个key:    0.66%
# 前    500 个key:    2.30%
# 前  1,000 个key:    3.83%
# 前  2,000 个key:    6.21%
# 前  5,000 个key:   11.32%
# 前 10,000 个key:   17.32%
# 前 20,000 个key:   25.82%
# 前 50,000 个key:   41.88%
# 前 100,000 个key:  57.85%

# RocksDB2存储热数据(前threshold个key)，RocksDB1存储冷数据

# Cluster 49 - threshold 10: 热数据占0.11%
ROCKSDB1_MEMTABLE_MAP["49_0"]=${TOTAL_MEMTABLE_SIZE}
ROCKSDB2_MEMTABLE_MAP["49_0"]=0

# Cluster 49 - threshold 10: 热数据占0.08%
ROCKSDB2_MEMTABLE_MAP["49_10"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0008 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_10"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_10"]}))

# Cluster 49 - threshold 100: 热数据占0.57%
ROCKSDB2_MEMTABLE_MAP["49_100"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0057 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_100"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_100"]}))

# Cluster 49 - threshold 500: 热数据占2.02%
ROCKSDB2_MEMTABLE_MAP["49_500"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0202 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_500"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_500"]}))

# Cluster 49 - threshold 1000: 热数据占3.39%
ROCKSDB2_MEMTABLE_MAP["49_1000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0339 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_1000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_1000"]}))

# Cluster 49 - threshold 2000: 热数据占5.58%
ROCKSDB2_MEMTABLE_MAP["49_2000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0558 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_2000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_2000"]}))

# Cluster 49 - threshold 5000: 热数据占10.32%
ROCKSDB2_MEMTABLE_MAP["49_5000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.1032 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_5000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_5000"]}))

# Cluster 49 - threshold 10000: 热数据占15.94%
ROCKSDB2_MEMTABLE_MAP["49_10000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.1594 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_10000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_10000"]}))

# Cluster 49 - threshold 20000: 热数据占23.98%
ROCKSDB2_MEMTABLE_MAP["49_20000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.2398 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_20000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_20000"]}))

# Cluster 49 - threshold 50000: 热数据占39.38%
ROCKSDB2_MEMTABLE_MAP["49_50000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.3938 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_50000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_50000"]}))

# Cluster 49 - threshold 100000: 热数据占55.01%
ROCKSDB2_MEMTABLE_MAP["49_100000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.5501 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["49_100000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["49_100000"]}))



# Cluster 25 - threshold 0: 特殊情况，所有数据都在RocksDB1
ROCKSDB1_MEMTABLE_MAP["30_0"]=${TOTAL_MEMTABLE_SIZE}
ROCKSDB2_MEMTABLE_MAP["30_0"]=0

# Cluster 25 - threshold 10: 热数据占7.48%
ROCKSDB2_MEMTABLE_MAP["30_10"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.0748 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_10"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_10"]}))

# Cluster 25 - threshold 100: 热数据占22.95%
ROCKSDB2_MEMTABLE_MAP["30_100"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.2295 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_100"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_100"]}))

# Cluster 25 - threshold 500: 热数据占40.09%
ROCKSDB2_MEMTABLE_MAP["30_500"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.4009 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_500"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_500"]}))

# Cluster 25 - threshold 1000: 热数据占48.54%
ROCKSDB2_MEMTABLE_MAP["30_1000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.4854 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_1000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_1000"]}))

# Cluster 25 - threshold 2000: 热数据占57.20%
ROCKSDB2_MEMTABLE_MAP["30_2000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.5720 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_2000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_2000"]}))

# Cluster 25 - threshold 5000: 热数据占68.06%
ROCKSDB2_MEMTABLE_MAP["30_5000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.6806 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_5000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_5000"]}))

# Cluster 25 - threshold 10000: 热数据占75.40%
ROCKSDB2_MEMTABLE_MAP["30_10000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.7540 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_10000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_10000"]}))

# Cluster 25 - threshold 20000: 热数据占81.54%
ROCKSDB2_MEMTABLE_MAP["30_20000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.8154 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_20000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_20000"]}))

# Cluster 25 - threshold 50000: 热数据占87.71%
ROCKSDB2_MEMTABLE_MAP["30_50000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.8771 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_50000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_50000"]}))

# Cluster 25 - threshold 100000: 热数据占91.17%
ROCKSDB2_MEMTABLE_MAP["30_100000"]=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * 0.9117 / 1" | bc)
ROCKSDB1_MEMTABLE_MAP["30_100000"]=$(($TOTAL_MEMTABLE_SIZE - ${ROCKSDB2_MEMTABLE_MAP["30_100000"]}))

# --- 定义循环测试的参数列表 ---
Cluster_numS=(30 )
THRESHOLDS=(100 10000 70000 100000 )

# --- 创建日志目录 ---
# 使用日期和时间创建一个唯一的日志目录
LOG_DIR="Dual_RocksDB_SSD_test_dir64MiB"
mkdir -p ${LOG_DIR}
echo "Log files will be saved in: ${LOG_DIR}"
echo "======================================================"

# --- 使用嵌套 for 循环进行多次测试 ---
for Cluster_num in "${Cluster_numS[@]}"
do
    for threshold_val in "${THRESHOLDS[@]}"
    do
        # --- 根据循环变量,动态生成本次测试的路径 ---
        # 1. 生成数据文件路径
        DATA_FILE="/mnt/nvm/second_cluster${Cluster_num}.sort"
        
        # 2. 为本次测试生成唯一的数据库路径,包含 Cluster 和 threshold 信息
        ROCKSDB1_PATH="/mnt/db_test/rocks10B/rocksdb1_Cluster_${Cluster_num}_thresh_${threshold_val}"
        ROCKSDB2_PATH="/mnt/db_test/rocks10B/rocksdb2_Cluster_${Cluster_num}_thresh_${threshold_val}"
        
        # 3. 为本次测试生成唯一的日志文件路径
        LOG_FILE="${LOG_DIR}/Cluster_${Cluster_num}_thresh_${threshold_val}.log"
        
        # 检查数据文件是否存在
        if [ ! -f "$DATA_FILE" ]; then
            echo "Warning: Data file not found for Cluster=${Cluster_num} at path: ${DATA_FILE}"
            echo "Skipping this iteration."
            continue # 跳过本次循环,继续下一次
        fi

        if [ -f "$LOG_FILE" ]; then
            echo "Log file already exists for Cluster=${Cluster_num}, threshold=${threshold_val}: ${LOG_FILE}"
            echo "Skipping this iteration."
            continue
        fi

        # --- 在循环开始时检查并清理磁盘 ---
        BASE_STORAGE_DIR="/mnt/db_test/rocks10B"
        REQUIRED_FREE_SPACE_GB=20
        THRESHOLD_KB=$((REQUIRED_FREE_SPACE_GB * 1024 * 1024))  # 转换为KB

        # 2. 获取磁盘可用空间 (单位: KB)
        available_space_kb=$(df --output=avail "${BASE_STORAGE_DIR}/" | tail -n 1)
        echo "--------------------------------------------------"
        echo "Checking disk space on ${BASE_STORAGE_DIR}/"
        echo "Available space: $((available_space_kb / 1024 / 1024)) GB. Required: ${REQUIRED_FREE_SPACE_GB} GB."

        # 3. 判断空间是否不足
        if [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; then
            echo "Low disk space detected. Available space is less than ${REQUIRED_FREE_SPACE_GB}GB."
            echo "Starting cleanup of largest files in ${BASE_STORAGE_DIR}/ ..."
            
            # 4. 循环删除最大的文件，直到空间足够
            while [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; do
                # 找到目录下最大的文件（递归查找所有文件）
                largest_file=$(find "${BASE_STORAGE_DIR}/" -type f -exec ls -la {} + 2>/dev/null | \
                            awk '{print $5 " " $9}' | \
                            sort -nr | \
                            head -1 | \
                            awk '{print $2}')
                
                # 如果没有找到文件，退出循环
                if [ -z "$largest_file" ] || [ ! -f "$largest_file" ]; then
                    echo "No more files to delete in ${BASE_STORAGE_DIR}/"
                    break
                fi
                
                # 获取文件大小（以MB为单位显示）
                file_size_bytes=$(stat -c%s "$largest_file" 2>/dev/null || echo 0)
                file_size_mb=$((file_size_bytes / 1024 / 1024))
                
                echo "Deleting largest file: $largest_file (Size: ${file_size_mb} MB)"
                
                # 删除文件
                rm -f "$largest_file"
                
                # 重新检查磁盘空间
                available_space_kb=$(df --output=avail "${BASE_STORAGE_DIR}/" | tail -n 1)
                echo "After deletion - Available space: $((available_space_kb / 1024 / 1024)) GB"
                
                # 防止无限循环，如果删除文件后空间没有明显增加，可能有其他问题
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
        # 使用子 shell 和 tee 命令,将所有输出 (包括标准输出和标准错误) 
        # 同时显示在屏幕上并写入到日志文件中。
        (
            # 清理本次测试可能残留的旧数据库文件
            echo "Cleaning up old database directories for Cluster=${Cluster_num}, threshold=${threshold_val}..."
            rm -rf ${ROCKSDB1_PATH}
            rm -rf ${ROCKSDB2_PATH}
            
            # 确保父目录存在
            mkdir -p $(dirname ${ROCKSDB1_PATH})
            mkdir -p $(dirname ${ROCKSDB2_PATH})

            MAP_KEY="${Cluster_num}_${threshold_val}"

            # 获取当前测试的memtable大小
            ROCKSDB1_MEMTABLE_SIZE=$TOTAL_MEMTABLE_SIZE
            ROCKSDB2_MEMTABLE_SIZE=$TOTAL_MEMTABLE_SIZE
            
            echo "Cluster ${Cluster_num}, threshold ${threshold_val}:"
            echo "  RocksDB1 (冷数据): $(($ROCKSDB1_MEMTABLE_SIZE / 1024 / 1024))MB"
            echo "  RocksDB2 (热数据): $(($ROCKSDB2_MEMTABLE_SIZE / 1024 / 1024))MB"
            
            echo "======================================================"
            echo "  STARTING TEST FOR Cluster = ${Cluster_num}, THRESHOLD = ${threshold_val}"
            echo "------------------------------------------------------"
            echo "  - Log File:       ${LOG_FILE}"
            echo "  - Data File:      ${DATA_FILE}"
            echo "  - RocksDB1 Path:  ${ROCKSDB1_PATH}"
            echo "  - RocksDB2 Path:  ${ROCKSDB2_PATH}"
            echo "  - Total Keys:     ${NUM_KEYS}"
            echo "  - Value Size:     ${VALUE_SIZE} bytes"
            echo "  - Threshold:      ${threshold_val}"
            echo "======================================================"
            
            # 执行测试,传递所有参数
            $EXECUTABLE \
                --num_keys=${NUM_KEYS} \
                --value_size=${VALUE_SIZE} \
                --threshold=${threshold_val} \
                --batch_size=${BATCH_SIZE} \
                --num_reads=${NUM_READS} \
                --data_file=${DATA_FILE} \
                --rocksdb1_path=${ROCKSDB1_PATH} \
                --rocksdb1_memtable_size=${ROCKSDB1_MEMTABLE_SIZE} \
                --rocksdb1_level0_file_num_compaction_trigger=${ROCKSDB1_L0_TRIGGER} \
                --rocksdb1_level0_slowdown_writes_trigger=${ROCKSDB1_L0_SLOWDOWN} \
                --rocksdb1_level0_stop_writes_trigger=${ROCKSDB1_L0_STOP} \
                --rocksdb1_max_bytes_for_level_base=${ROCKSDB1_MAX_BYTES_BASE} \
                --rocksdb1_max_bytes_for_level_multiplier=${ROCKSDB1_MAX_BYTES_MULT} \
                --rocksdb1_block_cache_size=${ROCKSDB1_BLOCK_CACHE} \
                --rocksdb1_max_background_jobs=${ROCKSDB1_MAX_JOBS} \
                --rocksdb2_path=${ROCKSDB2_PATH} \
                --rocksdb2_memtable_size=${ROCKSDB2_MEMTABLE_SIZE} \
                --rocksdb2_level0_file_num_compaction_trigger=${ROCKSDB2_L0_TRIGGER} \
                --rocksdb2_level0_slowdown_writes_trigger=${ROCKSDB2_L0_SLOWDOWN} \
                --rocksdb2_level0_stop_writes_trigger=${ROCKSDB2_L0_STOP} \
                --rocksdb2_max_bytes_for_level_base=${ROCKSDB2_MAX_BYTES_BASE} \
                --rocksdb2_max_bytes_for_level_multiplier=${ROCKSDB2_MAX_BYTES_MULT} \
                --rocksdb2_block_cache_size=${ROCKSDB2_BLOCK_CACHE} \
                --rocksdb2_max_background_jobs=${ROCKSDB2_MAX_JOBS}
        ) 2>&1 | tee "${LOG_FILE}"
        
        # 在两次测试之间添加一些空行,使输出更清晰
        echo -e "\n\n"
    done
done

echo "All test runs finished. Check the '${LOG_DIR}' directory for detailed logs."