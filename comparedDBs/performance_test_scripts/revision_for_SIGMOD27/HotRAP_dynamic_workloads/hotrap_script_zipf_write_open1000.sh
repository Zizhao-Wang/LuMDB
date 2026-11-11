#!/usr/bin/env bash
# 按 LevelDB 写入脚本组织负载；直接从 data_file_path 读取并写入 HotRAP。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$SCRIPT_DIR" || exit 1

# 按论文 §4.1 的比例设置：FD:SD=1:10，初始热集/RALT 为 FD 的 50%/15%。
# 本组 RocksDB 日志的 SST 总量约 213 GiB，以此估算并取整为 220 GiB 分层预算。
# 使用 GiB（1024^3 字节）；快层 tier0 和 RALT 放在 PM，慢层 tier1 放在 NVM。
tier0_size=$((20 * 1024 * 1024 * 1024))
tier1_size=$((tier0_size * 10))
max_hot_set_size=$((tier0_size * 50 / 100))
max_ralt_size=$((tier0_size * 15 / 100))

ulimit -n 800000 2>/dev/null || true

BASE_VALUE_SIZE=128
billion=1000000000
DEVICE_NAME="nvme0n1"
PM_DEVICE_NAME="pmem0"
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

# 本次并发配置使用独立的日志和数据库目录，保留之前的实验结果。
log_dir="HotRAP_Dynamic_zipf_PM_NVM_PaperRatio_FD20GiB_SD200GiB_BG6_F1_workloads_open1000"

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 128; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        zipf_as=(1.3 1.1)
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

            log_file="HotRAP_${num_format}_val${value_size}_zipf${zipf_tag}_mem${wbs_mb}MB.log"
            memory_log_file="$(pwd)/HotRAP_memory_usage_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_zipf${zipf_tag}.log"

            if [ -f "$log_file" ]; then
                echo "Log file $log_file already exists. Skipping..."
                continue
            fi

            db_dir="/mnt/nvm/hotrap10B/db/write_10B_zipf${zipf_tag}_mem${wbs_mb}MB_pm_nvm_paperratio_fd20gib_sd200gib_bg6_f1"
            case_name="${db_dir##*/}"
            tier0_dir="/mnt/pmem0/hotrap10B/tier0/$case_name"
            tier1_dir="/mnt/nvm/hotrap10B/tier1/$case_name"
            ralt_dir="/mnt/pmem0/hotrap10B/ralt/$case_name"
            mkdir -p "$tier0_dir" "$tier1_dir" "$ralt_dir"
            rm -rf "${tier0_dir:?}/"* "${tier1_dir:?}/"* "${ralt_dir:?}/"*
            if [ ! -d "$db_dir" ]; then
                mkdir -p "$db_dir"
            fi

            if [ "$(ls -A "$db_dir")" ]; then
                rm -rf "${db_dir:?}/"*
            fi


            echo "=================================================="
            echo "Running HotRAP zipf multi-workload write with parameters:"
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
            echo "  tier0_dir (PM) = $tier0_dir"
            echo "  tier1_dir (NVM)= $tier1_dir"
            echo "  ralt_dir (PM)  = $ralt_dir"
            echo "  tier0_size     = $tier0_size bytes (20 GiB)"
            echo "  tier1_size     = $tier1_size bytes (200 GiB)"
            echo "  hot_set_limit  = $max_hot_set_size bytes (10 GiB, 50% of FD)"
            echo "  ralt_limit     = $max_ralt_size bytes (3 GiB, 15% of FD)"
            echo "  wbs_bytes      = $wbs_bytes (${wbs_mb}MB)"
            echo "  max_file_size  = $max_file_size_bytes"
            echo "  background     = 6 compactions, 1 flush"
            echo "=================================================="

            iostat_log="HotRAP_${num_format}_val_${value_size}_zipf${zipf_tag}_mem${wbs_mb}MB_IOstats.log"
            iostat -d 100 -x "$DEVICE_NAME" "$PM_DEVICE_NAME" > "$iostat_log" &
            PID_IOSTAT=$!

            "$DB_BENCH" \
                --db="$db_dir" \
                --db_paths="[{$tier0_dir,$tier0_size},{$tier1_dir,$tier1_size}]" \
                --ralt_path="$ralt_dir" \
                --max_hot_set_size="$max_hot_set_size" \
                --max_ralt_size="$max_ralt_size" \
                --compaction_style=0 \
                --ttl_seconds=0 \
                --level_compaction_dynamic_level_bytes=false \
                --num="$total_num_entries" \
                --value_size="$value_size" \
                --value_size_="$value_size" \
                --batch_size=1 \
                --benchmarks=fillzipf,stats \
                --data_file_path="$data_file" \
                --bloom_bits=10 \
                --cache_size=8388608 \
                --mem_log_file="$memory_log_file" \
                --open_files=1000 \
                --max_background_jobs=7 \
                --max_background_compactions=6 \
                --max_background_flushes=1 \
                --compression_type=none \
                --stats_interval="$stats_interval_ops" \
                --stats_per_interval=1 \
                --histogram=1 \
                --write_buffer_size="$wbs_bytes" \
                --target_file_size_base="$max_file_size_bytes" \
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
    cd ..
done
