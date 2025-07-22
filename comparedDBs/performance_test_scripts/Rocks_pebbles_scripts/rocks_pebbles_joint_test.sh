#!/bin/bash

# 这是一个用于运行 RocksDB 和 LevelDB 联合测试的自动化脚本
# 它可以通过嵌套 for 循环，针对不同的 Zipfian 参数和阈值进行多次测试，
# 并将每次测试的完整输出记录到独立的日志文件中。
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
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

# --- 定义循环测试的参数列表 ---
# 在这里定义你想要测试的所有 Zipfian 参数和 Threshold
ZIPF_PARAMS=(1.1 1.3)
THRESHOLDS=(2000 10000 100000) # 你可以添加更多阈值，例如：(10 100000 5000000)

# --- 创建日志目录 ---
# 使用日期和时间创建一个唯一的日志目录
LOG_DIR="Rocks_Level_test_dir"
mkdir -p ${LOG_DIR}
echo "Log files will be saved in: ${LOG_DIR}"
echo "======================================================"


# --- 使用嵌套 for 循环进行多次测试 ---
for zipf_param in "${ZIPF_PARAMS[@]}"
do
    for threshold_val in "${THRESHOLDS[@]}"
    do
        # --- 根据循环变量，动态生成本次测试的路径 ---
        # 1. 生成数据文件路径
        DATA_FILE="/mnt/nvm/zipf${zipf_param}_keys10.0B.csv"
        
        # 2. 为本次测试生成唯一的数据库路径，包含 zipf 和 threshold 信息
        ROCKSDB_PATH="/mnt/workloads/rocks10B/rocksdb_zipf_${zipf_param}_thresh_${threshold_val}"
        LEVELDB_PATH="/mnt/workloads/pebbles10B/leveldb_zipf_${zipf_param}_thresh_${threshold_val}"

        # 3. 为本次测试生成唯一的日志文件路径
        LOG_FILE="${LOG_DIR}/zipf_${zipf_param}_thresh_${threshold_val}.log"

        # 检查数据文件是否存在
        if [ ! -f "$DATA_FILE" ]; then
            echo "Warning: Data file not found for zipf=${zipf_param} at path: ${DATA_FILE}"
            echo "Skipping this iteration."
            continue # 跳过本次循环，继续下一次
        fi
        if [ -f "$LOG_FILE" ]; then
            echo "Log file already exists for zipf=${zipf_param}, threshold=${threshold_val}: ${LOG_FILE}"
            echo "Skipping this iteration."
            continue
        fi 
        # --- 执行并记录日志 ---
        # 使用子 shell 和 tee 命令，将所有输出 (包括标准输出和标准错误) 
        # 同时显示在屏幕上并写入到日志文件中。
        (
            # 清理本次测试可能残留的旧数据库文件
            echo "Cleaning up old database directories for zipf=${zipf_param}, threshold=${threshold_val}..."
            rm -rf ${ROCKSDB_PATH}
            rm -rf ${LEVELDB_PATH}

            # 确保父目录存在
            mkdir -p $(dirname ${ROCKSDB_PATH})
            mkdir -p $(dirname ${LEVELDB_PATH})

            echo "======================================================"
            echo "  STARTING TEST FOR ZIPF = ${zipf_param}, THRESHOLD = ${threshold_val}"
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
