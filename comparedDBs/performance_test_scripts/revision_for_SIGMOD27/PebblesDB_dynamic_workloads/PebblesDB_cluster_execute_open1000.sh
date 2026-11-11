#!/usr/bin/env bash

sudo bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'

BASE_VALUE_SIZE=128
billion=1000000000
DEVICE_NAME="nvme0n1"
MEM=64

convert_to_billion_format() {
    local num=$1
    local integer_part=$((num / billion))
    local decimal_part=$((num % billion))

    if ((decimal_part == 0)); then
        echo "${integer_part}B"
    else
        local formatted_decimal
        formatted_decimal=$(echo "scale=9; $decimal_part / $billion" | bc | sed 's/0*$//' | sed 's/\.$//')
        formatted_decimal=$(echo "$formatted_decimal" | cut -d'.' -f2)
        echo "${integer_part}.${formatted_decimal}B"
    fi
}

log_dir="PebblesDB_Dynamic_TwitterCluster_workloads_open1000"
sleep 5h

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 1038; do
        ops_per_workload=100000000
        clusters=(35 40 45)
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

        for wbs_mb in "$MEM"; do
            wbs_bytes=$((wbs_mb * 1024 * 1024))
            max_file_size_bytes=$wbs_bytes

            log_file="PebblesDB_execute_${num_format}_val${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}.log"
            memory_log_file="$(pwd)/PebblesDB_memoryusage_execute_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_cluster${cluster_tag}.log"

            if [ -f "$log_file" ]; then
                echo "Log file $log_file already exists. Skipping..."
                continue
            fi

            sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'

            db_dir="/mnt/nvm/pebbles10B/twitter_workloads_cluster${cluster_tag}_execute_mem${wbs_mb}MB"
            if [ ! -d "$db_dir" ]; then
                mkdir -p "$db_dir"
            fi

            if [ "$(ls -A "$db_dir")" ]; then
                rm -rf "${db_dir:?}/"*
            fi

            echo "=================================================="
            echo "Running PebblesDB dynamic twitter workload with parameters:"
            echo "  base_num              = $base_num"
            echo "  ops_per_workload      = $ops_per_workload"
            echo "  workload_count        = $workload_count"
            echo "  total_ycsb_num        = $total_ycsb_entries"
            echo "  value_size            = $value_size"
            echo "  stats_interval        = $stats_interval_ops"
            echo "  expected_stats_reports = $expected_report_count"
            echo "  num_format            = $num_format"
            echo "  cluster_seq           = ${clusters[*]}"
            echo "  YCSB_data_file        = $ycsb_data_file"
            echo "  db_dir                = $db_dir"
            echo "  wbs_bytes             = $wbs_bytes (${wbs_mb}MB)"
            echo "  max_file_size_bytes   = $max_file_size_bytes"
            echo "=================================================="

            iostat_log="PebblesDB_execute_${num_format}_val_${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}_IOstats.log"
            iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
            PID_IOSTAT=$!

            ../../../pebblesdb/release/db_bench \
                --db="$db_dir" \
                --num="$total_ycsb_entries" \
                --value_size="$value_size" \
                --ycsb_num="$total_ycsb_entries" \
                --batch_size=1 \
                --benchmarks=twitterexecutedynamic,stats \
                --use_existing_db=false \
                --bloom_bits=10 \
                --log=1 \
                --cache_size=8388608 \
                --open_files=1000 \
                --mem_log_file="$memory_log_file" \
                --compression=0 \
                --stats_interval="$stats_interval_ops" \
                --YCSB_data_file="$ycsb_data_file" \
                --histogram=1 \
                --write_buffer_size="$wbs_bytes" \
                --max_file_size="$max_file_size_bytes" \
                --print_wa=true \
                &> >(tee "$log_file") &

            sleep 1

            DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
            echo "DB_BENCH_PID: $DB_BENCH_PID"

            perf_file="perf_stat_execute_${num_format}_val_${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}.txt"
            perf stat -p "$DB_BENCH_PID" 2>&1 | tee "$perf_file" &
            PERF_PID=$!

            wait "$DB_BENCH_PID"

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
    done
    cd ..
done
