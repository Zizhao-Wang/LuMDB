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

log_dir="leveldb_Dynamic_zipf_workloads_open1000"

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 128; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        zipf_as=( 1.3 1.1)
        data_files=()
        for zipf_a in "${zipf_as[@]}"; do
            data_files+=("/mnt/workloads/zipf${zipf_a}_keys10.0B.csv")
        done

        data_file=$(IFS=,; echo "${data_files[*]}")
        zipf_tag=$(IFS=_; echo "${zipf_as[*]}")
        workload_count=${#zipf_as[@]}
        total_num_entries=$((num_entries * workload_count))
        stats_interval_ops=$((total_num_entries / 20))
        num_format=$(convert_to_billion_format "$num_entries")

        for wbs_mb in "$MEM"; do
            wbs_bytes=$((wbs_mb * 1024 * 1024))
            max_file_size_bytes=$wbs_bytes

            log_file="leveldb_${num_format}_val${value_size}_zipf${zipf_tag}_mem${wbs_mb}MB.log"
            memory_log_file="$(pwd)/leveldb_memory_usage_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_zipf${zipf_tag}.log"

            if [ -f "$log_file" ]; then
                echo "Log file $log_file already exists. Skipping..."
                continue
            fi

            db_dir="/mnt/nvm/level10B/write_10B_zipf${zipf_tag}_mem${wbs_mb}MB"
            if [ ! -d "$db_dir" ]; then
                mkdir -p "$db_dir"
            fi

            if [ "$(ls -A "$db_dir")" ]; then
                rm -rf "${db_dir:?}/"*
            fi

            sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'

            echo "=================================================="
            echo "Running LevelDB zipf multi-workload write with parameters:"
            echo "  base_num       = $base_num"
            echo "  num_entries    = $num_entries"
            echo "  workload_count = $workload_count"
            echo "  total_num      = $total_num_entries"
            echo "  value_size     = $value_size"
            echo "  stats_interval = $stats_interval_ops"
            echo "  num_format     = $num_format"
            echo "  zipf_seq       = ${zipf_as[*]}"
            echo "  data_file      = $data_file"
            echo "  db_dir         = $db_dir"
            echo "  wbs_bytes      = $wbs_bytes (${wbs_mb}MB)"
            echo "  max_file_size  = $max_file_size_bytes"
            echo "=================================================="

            iostat_log="leveldb_${num_format}_val_${value_size}_zipf${zipf_tag}_mem${wbs_mb}MB_IOstats.log"
            iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
            PID_IOSTAT=$!

            ../../../leveldb/release/db_bench \
                --db="$db_dir" \
                --num="$total_num_entries" \
                --value_size="$value_size" \
                --batch_size=1 \
                --benchmarks=fillzipfdynamic,stats \
                --data_file="$data_file" \
                --bloom_bits=10 \
                --log=1 \
                --cache_size=8388608 \
                --mem_log_file="$memory_log_file" \
                --open_files=1000 \
                --compression=false \
                --stats_interval="$stats_interval_ops" \
                --histogram=1 \
                --write_buffer_size="$wbs_bytes" \
                --max_file_size="$max_file_size_bytes" \
                --print_wa=true \
                &> >(tee "$log_file") &

            sleep 1

            DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
            echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

            perf_file="perf_stat_${num_format}_val_${value_size}_zipf${zipf_tag}_mem${wbs_mb}MB.txt"
            perf stat -p "$DB_BENCH_PID" 2>&1 | tee "$perf_file" &
            PERF_PID=$!

            wait "$DB_BENCH_PID"

            echo "Checking if iostat process with PID $PID_IOSTAT is still running..."
            if ps -p "$PID_IOSTAT" > /dev/null; then
                echo "iostat process $PID_IOSTAT is still running, killing now..."
                kill -9 "$PID_IOSTAT"
                if [ $? -eq 0 ]; then
                    echo "iostat process $PID_IOSTAT has been successfully killed."
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
