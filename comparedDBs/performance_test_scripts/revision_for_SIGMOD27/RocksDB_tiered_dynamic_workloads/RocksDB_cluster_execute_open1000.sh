
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
MEM=512   # 你的 MEM 大小（MB）

log_dir="RocksDB_Dynamic_TwitterCluster_workloads_open1000"


for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 1038; do
        # 每个 workload 文件执行的操作数；3 个文件时总量=3*ops_per_workload
        ops_per_workload=100000000
        clusters=(35 40 37)
        ycsb_data_files=()
        for cluster in "${clusters[@]}"; do
            ycsb_data_files+=("/mnt/workloads/second_cluster${cluster}.sort")
        done
        ycsb_data_file=$(IFS=,; echo "${ycsb_data_files[*]}")
        cluster_tag=$(IFS=_; echo "${clusters[*]}")
        workload_count=${#clusters[@]}
        total_ycsb_entries=$((ops_per_workload * workload_count))
        stats_interval_ops=10000000
        expected_report_count=$((total_ycsb_entries / stats_interval_ops))
        num_format=$(convert_to_billion_format "$total_ycsb_entries")

        # 只用一个循环，同时设置 write_buffer_size 和 target_file_size_base
        for wbs_mb in 64; do
            # 将 MB 转换为字节
            wbs_bytes=$((wbs_mb * 1024 * 1024))
            # target_file_size_base 同样用这个值
            tfsb_bytes=$wbs_bytes

            # 在日志文件名加上 workload 序列
            log_file="RocksDB_execute_${num_format}_val${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}.log"
            memory_log_file="$(pwd)/RocksDB_memoryusage_execute_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_cluster${cluster_tag}.log"

            # 如果日志文件已存在，跳过
            if [ -f "$log_file" ]; then
                echo "Log file $log_file already exists. Skipping..."
                continue
            fi

            sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'
            db_dir="/mnt/nvm/rocks10B/twitter_workloads_cluster${cluster_tag}_write_mem${wbs_mb}MB"
            if [ ! -d "$db_dir" ]; then
                mkdir -p "$db_dir"
            fi

            # 如果目录不空，清空目录，确保执行 workload 时使用新数据库
            if [ "$(ls -A "$db_dir")" ]; then
                rm -rf "${db_dir:?}/"*
            fi

            echo "=================================================="
            echo "Running test with parameters:"
            echo "  base_num       = $base_num"
            echo "  ops_per_workload = $ops_per_workload"
            echo "  workload_count = $workload_count"
            echo "  total_ycsb_num = $total_ycsb_entries"
            echo "  value_size     = $value_size"
            echo "  stats_interval = $stats_interval_ops"
            echo "  expected_stats_reports = $expected_report_count"
            echo "  num_format     = $num_format"
            echo "  cluster_seq    = ${clusters[*]}"
            echo "  YCSB_data_file = $ycsb_data_file"
            echo "  db_dir         = $db_dir"
            echo "  wbs_bytes      = $wbs_bytes  (${wbs_mb}MB)"
            echo "  tfsb_bytes     = $tfsb_bytes (${wbs_mb}MB)"
            echo "=================================================="

            # 启动 iostat
            iostat_log="RocksDB_execute_${num_format}_val_${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}_IOstats.log"
            iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
            PID_IOSTAT=$!

            # 运行 db_bench
            ../../../rocksdb/release/db_bench \
                --db="$db_dir" \
                --num="$total_ycsb_entries" \
                --value_size_="$value_size" \
                --batch_size=1 \
                --benchmarks=twitterexecutedynamic,stats \
                --compaction_style=1 \
                --use_existing_db=false \
                --bloom_bits=10 \
                --cache_size=8388608 \
                --open_files=1000 \
                --compression_ratio=0 \
                --YCSB_data_file="$ycsb_data_file" \
                --stats_interval="$stats_interval_ops" \
                --stats_per_interval=1 \
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
            perf_file="perf_stat_execute_${num_format}_val_${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}.txt"
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

        done  # end of wbs_mb loop
    done          # end of value_size loop
    cd ..
done
