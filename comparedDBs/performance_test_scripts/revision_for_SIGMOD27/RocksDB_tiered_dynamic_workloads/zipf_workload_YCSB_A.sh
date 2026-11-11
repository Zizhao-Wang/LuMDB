#!/usr/bin/env bash

sudo bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'

BASE_VALUE_SIZE=128
billion=1000000000
DEVICE_NAME="sdc"

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

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_RocksDB_tiered_YCSB_A_after_zipf_preload_Open1000_Mthread"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 128; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        num_format=$(convert_to_billion_format "$num_entries")
        num_entries=1000000000
        ycsb_num_entries=10000000
        stats_interva=$((ycsb_num_entries / 10))
        ycsb_data_file="/mnt/workloads/ycsb_a_workload.csv"

        for iteration in {1..1}; do
            for zipf_a in 1.2; do
                data_file="/mnt/workloads/zipf${zipf_a}_keys10.0B.csv"

                for wbs_mb in 64; do
                    wbs_bytes=$((wbs_mb * 1024 * 1024))
                    tfsb_bytes=$wbs_bytes

                    db_dir="/mnt/nvm/rocks10B/ycsb_tiered_zipf${zipf_a}_mem${wbs_mb}MB_iterator_${iteration}"
                    log_file="RocksDB_tiered_YCSBA_${num_format}_val${value_size}_zipf${zipf_a}_mem${wbs_mb}MB_iterator${iteration}.log"
                    memory_log_file="$(pwd)/RocksDB_tiered_memoryusage_YCSBA_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_zipf${zipf_a}_iterator${iteration}.log"

                    if [ ! -d "$db_dir" ] || [ -z "$(ls -A "$db_dir")" ]; then
                        echo "Preload db_dir $db_dir does not exist or is empty. Skipping..."
                        continue
                    fi

                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping..."
                        continue
                    fi

                    sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'

                    echo "=================================================="
                    echo "Running YCSB-A test with parameters:"
                    echo "  base_num       = $base_num"
                    echo "  preload_num    = $num_entries"
                    echo "  ycsb_num       = $ycsb_num_entries"
                    echo "  value_size     = $value_size"
                    echo "  stats_interval = $stats_interva"
                    echo "  num_format     = $num_format"
                    echo "  zipf_a         = $zipf_a"
                    echo "  iteration      = $iteration"
                    echo "  preload_file   = $data_file"
                    echo "  YCSB_data_file = $ycsb_data_file"
                    echo "  db_dir         = $db_dir"
                    echo "  wbs_bytes      = $wbs_bytes  (${wbs_mb}MB)"
                    echo "  tfsb_bytes     = $tfsb_bytes (${wbs_mb}MB)"
                    echo "=================================================="

                    iostat_log="RocksDB_tiered_YCSBA_${num_format}_val_${value_size}_zipf${zipf_a}_mem${wbs_mb}MB_iterator${iteration}_IOstats.log"
                    iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
                    PID_IOSTAT=$!

                    ../../../../rocksdb/release/db_bench \
                        --db="$db_dir" \
                        --num="$num_entries" \
                        --value_size_="$value_size" \
                        --batch_size=1 \
                        --benchmarks=ycsba,stats \
                        --data_file_path="$data_file" \
                        --use_existing_db=true \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --open_files=1000 \
                        --max_background_compactions=6 \
                        --max_background_flushes=1 \
                        --compaction_style=1 \
                        --compression_ratio=0 \
                        --YCSB_data_file="$ycsb_data_file" \
                        --ycsb_num="$ycsb_num_entries" \
                        --stats_interval="$stats_interva" \
                        --stats_per_interval="$stats_interva" \
                        --histogram=1 \
                        --write_buffer_size="$wbs_bytes" \
                        --mem_log_file="$memory_log_file" \
                        --target_file_size_base="$tfsb_bytes" \
                        --compression_type=none \
                        &> >(tee "$log_file") &

                    # sleep 1

                    # DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                    # echo "DB_BENCH_PID: $DB_BENCH_PID"

                    # perf_file="perf_stat_YCSBA_${num_format}_val_${value_size}_zipf${zipf_a}_mem${wbs_mb}MB_iterator${iteration}.txt"
                    # perf stat -p "$DB_BENCH_PID" 2>&1 | tee "$perf_file" &
                    # PERF_PID=$!

                    # wait "$DB_BENCH_PID"

                    # echo "Checking iostat process $PID_IOSTAT..."
                    # if ps -p "$PID_IOSTAT" > /dev/null; then
                    #     echo "iostat process is still running, killing now..."
                    #     kill -9 "$PID_IOSTAT"
                    #     if [ $? -eq 0 ]; then
                    #         echo "iostat process $PID_IOSTAT killed."
                    #     else
                    #         echo "Failed to kill iostat process $PID_IOSTAT."
                    #     fi
                    # else
                    #     echo "iostat process $PID_IOSTAT is no longer running."
                    # fi
                done
            done
        done
    done
    cd ..
done
