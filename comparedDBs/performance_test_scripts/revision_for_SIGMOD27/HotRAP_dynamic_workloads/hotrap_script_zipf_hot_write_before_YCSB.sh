#!/usr/bin/env bash
# 与 RocksDB 脚本相同的实验流程，直接执行 HotRAP db_bench。
# 两个存储路径都放在 /mnt/nvm，HotRAP 使用 level compaction（style=0）。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$SCRIPT_DIR" || exit 1
ulimit -n 800000 2>/dev/null || true

# 容量单位为字节；tier0 与 tier1 是同一 NVM 上的两个不同目录。
tier0_size=$((10 * 1024 * 1024 * 1024))
tier1_size=$((1024 * 1024 * 1024 * 1024))
max_hot_set_size=$((2 * 1024 * 1024 * 1024))
max_ralt_size=$((512 * 1024 * 1024))

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
    dir1="${i}B_HotRAP_zipf_hot_before_YCSB_Open1000_Mthread"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 128; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        num_format=$(convert_to_billion_format "$num_entries")
        num_entries=1000000000
        stats_interva=$((num_entries / 10))

        for iteration in {1..1}; do
            for zipf_a in 1.1; do
                data_file="/mnt/workloads/zipf${zipf_a}_keys10.0B.csv"

                for wbs_mb in 64; do
                    wbs_bytes=$((wbs_mb * 1024 * 1024))
                    tfsb_bytes=$wbs_bytes

                    log_file="HotRAP_${num_format}_val${value_size}_zipf${zipf_a}_mem${wbs_mb}MB_iterator${iteration}.log"
                    memory_log_file="$(pwd)/HotRAP_memoryusage_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_zipf${zipf_a}_iterator${iteration}.log"

                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration..."
                        continue
                    fi

                    db_dir="/mnt/nvm/hotrap10B/db/ycsb_tiered_zipf${zipf_a}_mem${wbs_mb}MB_iterator_${iteration}"
                    case_name="${db_dir##*/}"
                    tier0_dir="/mnt/nvm/hotrap10B/tier0/$case_name"
                    tier1_dir="/mnt/nvm/hotrap10B/tier1/$case_name"
                    ralt_dir="/mnt/nvm/hotrap10B/ralt/$case_name"
                    mkdir -p "$tier0_dir" "$tier1_dir" "$ralt_dir"
                    rm -rf "${tier0_dir:?}/"* "${tier1_dir:?}/"* "${ralt_dir:?}/"*
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    if [ "$(ls -A "$db_dir")" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    echo "=================================================="
                    echo "Running test with parameters:"
                    echo "  base_num       = $base_num"
                    echo "  num_entries    = $num_entries"
                    echo "  value_size     = $value_size"
                    echo "  stats_interval = $stats_interva"
                    echo "  num_format     = $num_format"
                    echo "  zipf_a         = $zipf_a"
                    echo "  iteration      = $iteration"
                    echo "  data_file_path = $data_file"
                    echo "  wbs_bytes      = $wbs_bytes  (${wbs_mb}MB)"
                    echo "  tfsb_bytes     = $tfsb_bytes (${wbs_mb}MB)"
                    echo "=================================================="

                    iostat_log="HotRAP_${num_format}_val_${value_size}_zipf${zipf_a}_mem${wbs_mb}MB_iterator${iteration}_IOstats.log"
                    iostat -d 100 -x "$DEVICE_NAME" > "$iostat_log" &
                    PID_IOSTAT=$!

                    "$DB_BENCH" \
                        --db="$db_dir" \
                        --db_paths="[{$tier0_dir,$tier0_size},{$tier1_dir,$tier1_size}]" \
                        --ralt_path="$ralt_dir" \
                        --max_hot_set_size="$max_hot_set_size" \
                        --max_ralt_size="$max_ralt_size" \
                        --ttl_seconds=0 \
                        --level_compaction_dynamic_level_bytes=false \
                        --num="$num_entries" \
                        --value_size_="$value_size" \
                        --batch_size=1 \
                        --benchmarks=fillzipf,stats \
                        --data_file_path="$data_file" \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --open_files=1000 \
                        --max_background_compactions=6 \
                        --max_background_flushes=1 \
                        --compaction_style=0 \
                        --compression_ratio=0 \
                        --stats_interval="$stats_interva" \
                        --stats_per_interval="$stats_interva" \
                        --histogram=1 \
                        --write_buffer_size="$wbs_bytes" \
                        --mem_log_file="$memory_log_file" \
                        --target_file_size_base="$tfsb_bytes" \
                        --compression_type=none \
                        &> >(tee "$log_file") &

                    DB_BENCH_PID=$!
                    perf_file="${log_file%.log}_perf_stat.txt"
                    PERF_PID=""
                    if command -v perf >/dev/null 2>&1; then
                        perf stat -p "$DB_BENCH_PID" -o "$perf_file" &
                        PERF_PID=$!
                    fi
                    wait "$DB_BENCH_PID"
                    status=$?
                    kill "$PID_IOSTAT" 2>/dev/null || true
                    wait "$PID_IOSTAT" 2>/dev/null || true
                    if [ -n "$PERF_PID" ]; then wait "$PERF_PID" 2>/dev/null || true; fi
                    if [ "$status" -ne 0 ]; then exit "$status"; fi

                done
            done
        done
    done
    cd ..
done
