#!/usr/bin/env bash
# 单独写入实验：每个 workload 新建数据库，并启动独立的 db_bench 进程。
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$SCRIPT_DIR" || exit 1

# 默认执行两个独立实验；也可以在命令行只指定一个 workload。
workloads=(1.1 1.3)
if [ "$#" -gt 0 ]; then
    workloads=("$@")
fi
num_entries=10000000000
num_tag="10B"
value_size=128
stats_interval_ops=$((num_entries / 10))
wbs_bytes=$((64 * 1024 * 1024))
DEVICE_NAME="nvme0n1"
PM_DEVICE_NAME="pmem0"
log_root="$SCRIPT_DIR/10B_HotRAP_Independent_zipf_PM_NVM_PaperRatio_open1000"
ulimit -n 800000 2>/dev/null || true

if [ ! -x "$DB_BENCH" ]; then
    echo "db_bench 不存在或不可执行：$DB_BENCH" >&2
    exit 1
fi

DB_BENCH_PID=""
PID_IOSTAT=""
PERF_PID=""
cleanup() {
    local pid
    for pid in "$DB_BENCH_PID" "$PID_IOSTAT" "$PERF_PID"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    DB_BENCH_PID=""
    PID_IOSTAT=""
    PERF_PID=""
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for workload in "${workloads[@]}"; do
    # 容量依据此 workload 单独从空库写入后的 SST 规模，不取两个负载的总量。
    case "$workload" in
        1.1)
            baseline_sst_gib=210.48
            baseline_source="Rocksdb_scripts/10B_RocksDB_zipf_hot_SATASSD/RocksDB_10B_val128_zipf1.1_mem64MB.log"
            tier0_mib=20480
            ;;
        1.2)
            baseline_sst_gib=43.91
            baseline_source="Rocksdb_scripts/10B_RocksDB_zipf_hot_SATASSD/RocksDB_10B_val128_zipf1.2_mem64MB.log"
            tier0_mib=4352
            ;;
        *)
            echo "支持的独立 Zipf workload：1.1、1.2；收到：$workload" >&2
            exit 2
            ;;
    esac
    workload_name="zipf$workload"
    data_file="/mnt/workloads/zipf${workload}_keys10.0B.csv"
    level_base_mib=256
    target_file_mib=64
    tier1_mib=$((tier0_mib * 10))
    tier0_size=$((tier0_mib * 1024 * 1024))
    tier1_size=$((tier1_mib * 1024 * 1024))
    max_hot_set_size=$((tier0_size * 50 / 100))
    max_ralt_size=$((tier0_size * 15 / 100))
    level_base_bytes=$((level_base_mib * 1024 * 1024))
    target_file_size_bytes=$((target_file_mib * 1024 * 1024))

    if [ ! -r "$data_file" ]; then
        echo "无法读取 workload：$data_file" >&2
        exit 1
    fi
    printf -v case_name 'single_%s_%s_val%s_mem64MB_fd%smib_sd%smib_lbase%smib_sst%smib_bg6_f1' \
        "$workload_name" "$num_tag" "$value_size" "$tier0_mib" "$tier1_mib" "$level_base_mib" "$target_file_mib"
    log_dir="$log_root/$case_name"
    log_file="$log_dir/HotRAP_$case_name.log"
    if [ -f "$log_file" ]; then
        echo "Log file $log_file already exists. Skipping..."
        continue
    fi

    db_dir="/mnt/nvm/hotrap10B/db/$case_name"
    tier0_dir="/mnt/pmem0/hotrap10B/tier0/$case_name"
    tier1_dir="/mnt/nvm/hotrap10B/tier1/$case_name"
    ralt_dir="/mnt/pmem0/hotrap10B/ralt/$case_name"
    # 只清理当前单负载实验专用目录，确保该 workload 从空库开始。
    rm -rf -- "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir" || exit 1
    mkdir -p "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir" "$log_dir" || exit 1

    {
        echo "=================================================="
        echo "Running independent HotRAP workload: $workload_name"
        echo "  data_file              = $data_file"
        echo "  num_entries            = $num_entries"
        echo "  value_size             = $value_size"
        echo "  baseline_sst_gib       = $baseline_sst_gib (estimated workload size)"
        echo "  baseline_source        = $baseline_source"
        echo "  stats_interval_ops     = $stats_interval_ops (10 points)"
        echo "  db_dir                 = $db_dir (fresh database)"
        echo "  tier0_dir (PM)         = $tier0_dir"
        echo "  tier1_dir (NVM)        = $tier1_dir"
        echo "  ralt_dir (PM)          = $ralt_dir"
        echo "  tier0_size             = $tier0_size bytes"
        echo "  tier1_size             = $tier1_size bytes (10 x FD)"
        echo "  hot_set_limit          = $max_hot_set_size bytes (50% of FD)"
        echo "  ralt_limit             = $max_ralt_size bytes (15% of FD)"
        echo "  level_base_bytes       = $level_base_bytes"
        echo "  target_file_size       = $target_file_size_bytes"
        echo "  write_buffer_size      = $wbs_bytes"
        echo "  background             = 6 compactions, 1 flush"
        echo "  first_slow_level       = L3 (planned by initial level sizes)"
        echo "=================================================="
    } | tee "$log_file"

    memory_log_file="$log_dir/HotRAP_memory_usage_$case_name.log"
    iostat_log="$log_dir/HotRAP_${case_name}_IOstats.log"
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
        --max_bytes_for_level_base="$level_base_bytes" \
        --num="$num_entries" \
        --threads=1 \
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
        --target_file_size_base="$target_file_size_bytes" \
        &> >(tee -a "$log_file") &
    DB_BENCH_PID=$!
    if command -v perf >/dev/null 2>&1; then
        perf stat -p "$DB_BENCH_PID" -o "$log_dir/HotRAP_${case_name}_perf_stat.txt" &
        PERF_PID=$!
    fi
    wait "$DB_BENCH_PID"
    status=$?
    DB_BENCH_PID=""
    cleanup
    if [ "$status" -ne 0 ]; then
        exit "$status"
    fi
done

