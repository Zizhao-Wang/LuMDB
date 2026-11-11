
sudo bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

MEM=1

convert_to_billion_format() {
    local num=$1
    local integer_part=$((num / billion))
    local decimal_part=$((num % billion))

    if ((decimal_part == 0)); then
        echo "${integer_part}B"
    else
        # Convert decimal part to the format of "x billionths"
        local formatted_decimal=$(echo "scale=9; $decimal_part / $billion" | bc | sed 's/0*$//' | sed 's/\.$//')
        # Extract only the fractional part after the decimal point
        formatted_decimal=$(echo $formatted_decimal | cut -d'.' -f2)
        echo "${integer_part}.${formatted_decimal}B"
    fi
}

#!/usr/bin/env bash

convert_to_billion_format() {
    # 这里是示例函数，你可以根据实际需要进行改动
    local num=$1
    if (( num >= 1000000000 )); then
        local billions=$((num / 1000000000))
        echo "${billions}B"
    else
        echo "${num}"
    fi
}

billion=1000000000
BASE_VALUE_SIZE=128
DEVICE_NAME="sdc"  # 你的设备名称
MEM=512                # 你的 MEM 大小（MB）

declare -A data_percentage_map
data_percentage_map["1_300"]="95"
data_percentage_map["1_500"]="90"
data_percentage_map["1_2000"]="85"

# 根据cluster和top_k获取数据比例的函数
get_data_percentage_for_topk() {
    local top_k=$1
    local cluster_num=$2
    local map_key="${cluster_num}_${top_k}"
    
    echo "DEBUG: 查找key=${map_key}" >&2
    echo "DEBUG: map中的所有key: ${!data_percentage_map[@]}" >&2
    echo "DEBUG: map中${map_key}的值: '${data_percentage_map[$map_key]}'" >&2
    
    # 在map中查找
    if [[ -n "${data_percentage_map[$map_key]}" ]]; then
        echo "DEBUG: 找到了值: ${data_percentage_map[$map_key]}" >&2
        echo "${data_percentage_map[$map_key]}"
    else
        echo "DEBUG: 没找到，使用默认值100" >&2
        # 如果map中没有找到，返回默认值（整数）
        echo "100"  # 改为100，表示100%，即不缩放
    fi
}

# 计算调整后的memtable大小
calculate_adjusted_memtable_size() {
    local base_size_mb=$1
    local data_percentage=$2
    local min_size_mb=1  # 最小memtable大小，防止过小
    
    echo "DEBUG: 输入参数 base_size_mb=$base_size_mb, data_percentage=$data_percentage" >&2
    
    # 如果数据比例为0，使用最小大小
    if [[ "$data_percentage" == "0" ]]; then
        echo "DEBUG: 数据比例为0，返回最小大小 $min_size_mb" >&2
        echo $min_size_mb
        return
    fi
    
    # 如果数据比例为100或更大，使用原始大小
    if [[ $data_percentage -ge 100 ]]; then
        echo "DEBUG: 数据比例 >= 100%，返回原始大小 $base_size_mb" >&2
        echo $base_size_mb
        return
    fi
    
    # 直接按数据比例调整大小 - 使用整数运算
    # 调整公式：adjusted_size = base_size * data_percentage / 100
    local adjusted_size=$((base_size_mb * data_percentage / 100))
    echo "DEBUG: 计算过程 $base_size_mb * $data_percentage / 100 = $adjusted_size" >&2
    
    # 确保不小于最小大小
    if [[ $adjusted_size -lt $min_size_mb ]]; then
        echo "DEBUG: 计算结果 $adjusted_size < 最小值 $min_size_mb，返回最小值" >&2
        echo $min_size_mb
    else
        echo "DEBUG: 最终结果 $adjusted_size" >&2
        echo $adjusted_size
    fi
}

# 显示当前映射表的函数（用于调试）
show_mapping_table() {
    echo "当前数据比例映射表："
    echo "===================="
    for key in $(printf '%s\n' "${!data_percentage_map[@]}" | sort); do
        echo "  $key -> ${data_percentage_map[$key]}%"
    done
    echo "===================="
    echo ""
}

# 测试计算
echo "测试memtable大小计算："
echo "===================="

REQUIRED_FREE_SPACE_GB=300
# 将GB转换为KB，方便后续比较
THRESHOLD_KB=$((REQUIRED_FREE_SPACE_GB * 1024 * 1024))

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_RocksDB_TwitterCluster_Removal_workloads"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 128; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        stats_interva=$((num_entries / 1000))
        num_format=$(convert_to_billion_format "$num_entries")
        num_entries=500000000

        for clsuternum_a in 13; do

            # 只用一个循环，同时设置 write_buffer_size 和 target_file_size_base
            for wbs_mb in 64; do
                # for top_k in 10 20 30 40 50 60 70 80 90 100; do
                # for top_k in 100 90 80 70 60 50 40 30 20 10; do
                for top_k in  1000 5000 10000 50000 100000 3128052 15640259 31280517; do

                   data_percentage=$(get_data_percentage_for_topk $top_k $clsuternum_a)
                    # 检查是否在map中找到了数据
                    map_key="${clusternum_a}_${top_k}"
                    if [[ -n "${data_percentage_map[$map_key]}" ]]; then
                        data_source="映射表"
                    else
                        data_source="默认值"
                    fi

                    # 计算调整后的memtable大小
                    adjusted_wbs_mb=$(calculate_adjusted_memtable_size $wbs_mb $data_percentage)
                    
                    # 将 MB 转换为字节
                    wbs_bytes=$((adjusted_wbs_mb * 1024 * 1024))

                    # target_file_size_base 同样用这个值
                    tfsb_bytes=$wbs_bytes

                    # 在日志文件名加上 wbs{MB}MB
                    log_file="RocksDB_${num_format}_val${value_size}_cluster${clsuternum_a}_mem${adjusted_wbs_mb}MB_Removal${top_k}.log"
                    data_file="/mnt/nvm/second_cluster${clsuternum_a}.sort"
                    memory_log_file="/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_TwitterCluster_workloads/RocksDB_memoryusage_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_cluster${clsuternum_a}_Removal${top_k}.log"

                    # 如果日志文件已存在，跳过
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping..."
                        continue
                    fi

                    # --- 在循环开始时检查并清理磁盘 ---
                    BASE_STORAGE_DIR="/mntdisk/rocks10B"
                    # 2. 获取磁盘可用空间 (单位: KB) - 使用前缀变量
                    available_space_kb=$(df --output=avail "${BASE_STORAGE_DIR}/" | tail -n 1)
                    echo "--------------------------------------------------"
                    echo "Checking disk space on ${BASE_STORAGE_DIR}/"
                    echo "Available space: $((available_space_kb / 1024 / 1024)) GB. Required: ${REQUIRED_FREE_SPACE_GB} GB."

                    # 3. 判断空间是否不足
                    if [ "$available_space_kb" -lt "$THRESHOLD_KB" ]; then
                        echo "Low disk space detected. Available space is less than ${REQUIRED_FREE_SPACE_GB}GB."
                        # 使用前缀变量
                        echo "Starting cleanup of ${BASE_STORAGE_DIR}/ ..."
                        
                        # 4. 执行清理：删除除了当前目录以外的所有其他目录
                        # 使用前缀变量
                        find "${BASE_STORAGE_DIR}/" -mindepth 1 -maxdepth 1 -type d | grep -v "^${current_db_dir}$" | while read -r dir_to_delete; do
                            if [ -d "$dir_to_delete" ]; then # 再次确认是目录
                                echo "Deleting old directory: $dir_to_delete"
                                rm -rf "$dir_to_delete"
                            fi
                        done
                        echo "Cleanup finished."
                    else
                        echo "Sufficient disk space available."
                    fi
                    # --- 磁盘检查与清理逻辑结束 ---

                    db_dir="/mntdisk/rocks10B/write_cluster${clsuternum_a}_mem${wbs_mb}MiB_Removaltop${top_k}"
                    
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 如果目录不空，清空目录
                    if [ "$(ls -A "$db_dir")" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    # Check if clsuternum_a is 19 and set num_entries accordingly
                    if [ "$clsuternum_a" -eq 19 ]; then
                        num_entries=600000000
                    fi

                    echo "=================================================="
                    echo "Running test with parameters:"
                    echo "  base_num       = $base_num"
                    echo "  num_entries    = $num_entries"
                    echo "  value_size     = $value_size"
                    echo "  stats_interval = $stats_interva"
                    echo "  num_format     = $num_format"
                    echo "  clsuterNum     = $clsuternum_a"
                    echo "  wbs_bytes      = $wbs_bytes  (${wbs_mb}MB)"
                    echo "  tfsb_bytes     = $tfsb_bytes (${wbs_mb}MB)"
                    echo "=================================================="

                    # 启动 iostat
                    iostat_log="RocksDB_${num_format}_val_${value_size}_cluster${clsuternum_a}_mem${wbs_mb}MB_Removal${top_k}_IOstats.log"
                    iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
                    PID_IOSTAT=$!

                    # 运行 db_bench
                    ../../../rocksdb/release/db_bench \
                        --db="$db_dir" \
                        --num="$num_entries" \
                        --value_size_="$value_size" \
                        --batch_size=1 \
                        --benchmarks=RemoveTopk,stats \
                        --data_file_path="$data_file" \
                        --bloom_bits=10 \
                        --TOP_K="$top_k" \
                        --cache_size=8388608 \
                        --open_files=80000 \
                        --compression_ratio=0 \
                        --stats_interval="$stats_interva" \
                        --stats_per_interval="$stats_interva" \
                        --histogram=1 \
                        --write_buffer_size="$wbs_bytes" \
                        --mem_log_file="$memory_log_file" \
                        --target_file_size_base="$tfsb_bytes" \
                        --compression_type=none \
                        &> >(tee "$log_file") &

                    # 等待1秒，让 db_bench 进程启动
                    sleep 1

                    # 找到 db_bench 的 PID 供 perf 监控
                    DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                    echo "DB_BENCH_PID: $DB_BENCH_PID"

                    # perf stat 监控
                    perf_file="perf_stat_${num_format}_val_${value_size}_cluster${clsuternum_a}_mem${wbs_mb}MB_Removal${top_k}.txt"
                    perf stat -p "$DB_BENCH_PID" 2>&1 | tee "$perf_file" &
                    PERF_PID=$!

                    # 等待 db_bench 进程结束
                    wait "$DB_BENCH_PID"

                    # 结束 iostat 进程
                    echo "Checking iostat process $PID_IOSTAT..."
                    if ps -p "$PID_IOSTAT" > /dev/null; then
                        echo "iostat process is still running, killing now..."
                        kill -9 "$PID_IOSTAT"
                        if [ $? -eq 0 ]; then
                            echo "iostat process $PID_IOSTAT killed."
                        else
                            echo "Failed to kill iostat process $PID_IOSTAT."
                        fi
                    else
                        echo "iostat process $PID_IOSTAT is no longer running."
                    fi
                done
            done  # end of wbs_mb loop
        done      # end of zipf_a loop
    done          # end of value_size loop
    cd ..
done




