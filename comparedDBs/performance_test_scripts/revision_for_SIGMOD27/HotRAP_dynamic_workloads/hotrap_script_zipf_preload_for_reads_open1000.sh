#!/usr/bin/env bash
# 为后续读实验预加载备用数据库：3 种 Zipf，各 3 份，每份独立写入 10 亿次。
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$SCRIPT_DIR" || exit 1

workloads=(1.3 1.4)
if [ "$#" -gt 0 ]; then
    workloads=("$@")
fi
iterations="${ITERATIONS:-3}"
iteration_start="${START_ITERATION:-1}"
num_entries=1000000000
value_size=128
seed=1
stats_interval_ops=$((num_entries / 10))
wbs_bytes=$((64 * 1024 * 1024))
log_root="$SCRIPT_DIR/1B_HotRAP_ReadPreload_zipf_PM_NVM_PaperRatio_open1000"
DEVICE_NAME="nvme0n1"
PM_DEVICE_NAME="pmem0"
ulimit -n 800000 2>/dev/null || true

if [[ ! "$iterations" =~ ^[1-9][0-9]*$ || ! "$iteration_start" =~ ^[1-9][0-9]*$ ]]; then
    echo "ITERATIONS 和 START_ITERATION 必须是正整数。" >&2
    exit 2
fi
iteration_end=$((iteration_start + iterations - 1))
if [ "$iteration_end" -lt "$iteration_start" ]; then
    echo "迭代编号超出整数范围。" >&2
    exit 2
fi
if [ ! -x "$DB_BENCH" ]; then
    echo "db_bench 不存在或不可执行：$DB_BENCH" >&2
    exit 1
fi
# 开始前校验全部输入，避免运行数小时后才发现下一个文件不正确。
for workload in "${workloads[@]}"; do
    case "$workload" in
        1.2|1.3|1.4) ;;
        *)
            echo "支持的 Zipf workload：1.2、1.3、1.4；收到：$workload" >&2
            exit 2
            ;;
    esac
    data_file="/mnt/workloads/zipf${workload}_keys10.0B.csv"
    if [ ! -r "$data_file" ]; then
        echo "无法读取 workload：$data_file" >&2
        exit 1
    fi
done

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
    # 参考现有 RocksDB 同一 CSV 在第 10 亿次写入时的 SST 存量，不用 100 亿次的最终量。
    # 参考日志：Rocksdb_scripts/10B_RocksDB_zipf_hot_SATASSD/RocksDB_10B_val128_zipf<theta>_mem64MB.log
    case "$workload" in
        1.2)
            baseline_sst_mib=6717.44  # 6.56 GiB
            tier0_mib=640
            level_base_mib=32
            target_file_mib=32
            ;;
        1.3)
            baseline_sst_mib=1689.60  # 1.65 GiB
            tier0_mib=160
            level_base_mib=8
            target_file_mib=8
            ;;
        1.4)
            baseline_sst_mib=633.87
            tier0_mib=64
            level_base_mib=4
            target_file_mib=4
            ;;
    esac
    tier1_mib=$((tier0_mib * 10))
    tier0_size=$((tier0_mib * 1024 * 1024))
    tier1_size=$((tier1_mib * 1024 * 1024))
    max_hot_set_size=$((tier0_size * 50 / 100))
    max_ralt_size=$((tier0_size * 15 / 100))
    level_base_bytes=$((level_base_mib * 1024 * 1024))
    target_file_size_bytes=$((target_file_mib * 1024 * 1024))
    data_file="/mnt/workloads/zipf${workload}_keys10.0B.csv"

    for ((iteration=iteration_start; iteration<=iteration_end; iteration++)); do
        printf -v iteration_tag '%02d' "$iteration"
        printf -v case_name 'read_preload_zipf%s_1B_val128_mem64MB_fd%smib_sd%smib_lbase%smib_sst%smib_bg6_f1_seed%s_iter%s' \
            "$workload" "$tier0_mib" "$tier1_mib" "$level_base_mib" "$target_file_mib" "$seed" "$iteration_tag"
        log_dir="$log_root/$case_name"
        log_file="$log_dir/HotRAP_$case_name.log"
        config_file="$log_dir/preload_config.env"
        complete_marker="$log_dir/PRELOAD_COMPLETE"
        db_dir="/mnt/nvm/hotrap1B_read_preload/db/$case_name"
        tier0_dir="/mnt/pmem0/hotrap1B_read_preload/tier0/$case_name"
        tier1_dir="/mnt/nvm/hotrap1B_read_preload/tier1/$case_name"
        ralt_dir="/mnt/pmem0/hotrap1B_read_preload/ralt/$case_name"
        db_paths="[{$tier0_dir,$tier0_size},{$tier1_dir,$tier1_size}]"

        if [ -f "$complete_marker" ]; then
            if [ ! -f "$db_dir/CURRENT" ] || [ ! -f "$config_file" ] || \
               [ ! -d "$tier0_dir" ] || [ ! -d "$tier1_dir" ] || [ ! -d "$ralt_dir" ]; then
                echo "完成标记存在，但数据库或配置缺失：$case_name。请选择其他迭代，不覆盖此副本。" >&2
                exit 1
            fi
            echo "Preload already complete. Skipping: $case_name"
            continue
        fi
        # 这些副本要留给未来读实验使用；已有内容即使未完成也不自动删除。
        for path in "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir" "$log_dir"; do
            if [ -e "$path" ]; then
                echo "已有未标记完成的目录：$path。请选择新迭代编号，或先自行归档该副本。" >&2
                exit 1
            fi
        done
        mkdir -p "$log_root" /mnt/nvm/hotrap1B_read_preload/db /mnt/nvm/hotrap1B_read_preload/tier1 \
            /mnt/pmem0/hotrap1B_read_preload/tier0 /mnt/pmem0/hotrap1B_read_preload/ralt || exit 1
        # 非 -p 的 mkdir 为当前副本预留目录；同时启动两份脚本时不会覆盖彼此。
        mkdir "$log_dir" || exit 1
        mkdir "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir" || exit 1

        # 保存重开数据库所需的路径与参数；后续读实验使用 --use_existing_db=true。
        {
            echo "# HotRAP read-preload configuration. See PRELOAD_COMPLETE for readiness."
            printf '%s=%q\n' \
                DB_BENCH "$DB_BENCH" ZIPF_A "$workload" ITERATION "$iteration" \
                DATA_FILE "$data_file" NUM_ENTRIES "$num_entries" VALUE_SIZE "$value_size" KEY_SIZE 16 SEED "$seed" \
                DB_DIR "$db_dir" DB_PATHS "$db_paths" TIER0_DIR "$tier0_dir" TIER1_DIR "$tier1_dir" RALT_DIR "$ralt_dir" \
                TIER0_SIZE "$tier0_size" TIER1_SIZE "$tier1_size" \
                MAX_HOT_SET_SIZE "$max_hot_set_size" MAX_RALT_SIZE "$max_ralt_size" \
                MAX_BYTES_FOR_LEVEL_BASE "$level_base_bytes" TARGET_FILE_SIZE_BASE "$target_file_size_bytes" \
                WRITE_BUFFER_SIZE "$wbs_bytes" CACHE_SIZE 8388608 OPEN_FILES 1000 \
                MAX_BACKGROUND_JOBS 7 MAX_BACKGROUND_COMPACTIONS 6 MAX_BACKGROUND_FLUSHES 1 \
                COMPACTION_STYLE 0 LEVEL_COMPACTION_DYNAMIC_LEVEL_BYTES false TTL_SECONDS 0 \
                COMPRESSION_TYPE none BLOOM_BITS 10 USE_EXISTING_DB true
        } > "$config_file" || exit 1
        {
            echo "=================================================="
            echo "Preparing HotRAP read dataset: zipf$workload, iteration $iteration"
            echo "  data_file             = $data_file (same first $num_entries operations for every copy)"
            echo "  num_entries           = $num_entries"
            echo "  value_size            = $value_size"
            echo "  seed                  = $seed"
            echo "  baseline_sst_mib      = $baseline_sst_mib (estimate at 1B operations)"
            echo "  tier0_size (PM)       = $tier0_size bytes"
            echo "  tier1_size (NVM)      = $tier1_size bytes (10 x FD)"
            echo "  hot_set_limit         = $max_hot_set_size bytes (50% of FD)"
            echo "  ralt_limit            = $max_ralt_size bytes (15% of FD)"
            echo "  level_base_bytes      = $level_base_bytes"
            echo "  target_file_size      = $target_file_size_bytes"
            echo "  write_buffer_size     = $wbs_bytes"
            echo "  background            = 6 compactions, 1 flush"
            echo "  db_dir                = $db_dir"
            echo "  tier0_dir             = $tier0_dir"
            echo "  tier1_dir             = $tier1_dir"
            echo "  ralt_dir              = $ralt_dir"
            echo "  config_file           = $config_file"
            echo "  stats_interval_ops    = $stats_interval_ops (10 points)"
            echo "=================================================="
        } | tee "$log_file"
        memory_log_file="$log_dir/HotRAP_memory_usage_$case_name.log"
        iostat -d 100 -x "$DEVICE_NAME" "$PM_DEVICE_NAME" > "$log_dir/HotRAP_${case_name}_IOstats.log" &
        PID_IOSTAT=$!

        "$DB_BENCH" \
            --db="$db_dir" \
            --db_paths="$db_paths" \
            --ralt_path="$ralt_dir" \
            --max_hot_set_size="$max_hot_set_size" \
            --max_ralt_size="$max_ralt_size" \
            --compaction_style=0 \
            --ttl_seconds=0 \
            --level_compaction_dynamic_level_bytes=false \
            --max_bytes_for_level_base="$level_base_bytes" \
            --num="$num_entries" \
            --threads=1 \
            --seed="$seed" \
            --value_size="$value_size" \
            --value_size_="$value_size" \
            --batch_size=1 \
            --benchmarks=fillzipf,flush,waitforcompaction,stats \
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
            echo "预加载失败，保留现场且不创建完成标记：$case_name" >&2
            exit "$status"
        fi
        if [ ! -f "$db_dir/CURRENT" ] || \
           ! grep -Eq "^fillzipf[[:space:]]*:.*[[:space:]]$num_entries operations;" "$log_file" || \
           ! grep -Fq "finished with status (OK)" "$log_file"; then
            echo "预加载结果校验失败，不创建完成标记：$case_name" >&2
            exit 1
        fi
        {
            echo "workload=zipf$workload"
            echo "iteration=$iteration"
            echo "operations=$num_entries"
            echo "completed_at=$(TZ=Asia/Shanghai date '+%F %T %Z')"
        } > "$complete_marker" || exit 1
        echo "Read dataset ready: $case_name"
        echo "Saved reopen configuration: $config_file"
    done
done
