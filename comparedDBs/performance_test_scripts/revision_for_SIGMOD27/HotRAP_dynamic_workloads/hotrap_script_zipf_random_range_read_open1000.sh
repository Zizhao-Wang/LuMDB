#!/usr/bin/env bash
# Zipf CSV 范围查询；与 PebblesDB 对齐：CSV key + 2000000，最多读取 20 条。
# 重开预加载库，沿用原来的 PM/SSD 路径及容量。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QUERY_DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench_read_csv}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
preload_root="${PRELOAD_ROOT:-$SCRIPT_DIR/1B_HotRAP_ReadPreload_zipf_PM_NVM_PaperRatio_open1000}"
log_root="${LOG_ROOT:-$SCRIPT_DIR/HotRAP_zipf_random_range_read_open1000}"
workload_root="${WORKLOAD_ROOT:-/mnt/workloads}"
iteration="${PRELOAD_ITERATION:-2}"
query_ops="${QUERY_OPS:-500000}"
scan_length="${SCAN_LENGTH:-20}"
scan_offset="${SCAN_KEY_OFFSET:-2000000}"
drop_caches="${DROP_CACHES:-1}"
dry_run="${DRY_RUN:-0}"
# 与参考查询脚本对齐；下列参数覆盖预加载时用于写入的配置。
query_cache_size=8388608
query_open_files=1000  # PebblesDB 为 1000；RocksDB 参考脚本为 80000。
query_write_buffer_size=1048576
query_target_file_size=1048576
stats_interval_ops=100000000  # 参考脚本先用 10B / 100 算出此值。
iostat_interval_seconds=100
workloads=(1.2 1.3 1.4)
if [ "$#" -gt 0 ]; then workloads=("$@"); fi
fail() { echo "$*" >&2; exit 1; }
[[ "$iteration" =~ ^[1-9][0-9]*$ && "$query_ops" =~ ^[1-9][0-9]*$ ]] || fail 'PRELOAD_ITERATION 和 QUERY_OPS 必须是正整数。'
[[ "$scan_length" =~ ^(0|[1-9][0-9]*)$ && "$scan_offset" =~ ^(0|[1-9][0-9]*)$ ]] || fail 'SCAN_LENGTH 和 SCAN_KEY_OFFSET 必须是非负整数。'
[ "$scan_length" -le 2147483647 ] || fail 'SCAN_LENGTH 超出 seek_nexts 的整数范围。'
[[ "$drop_caches" =~ ^[01]$ && "$dry_run" =~ ^[01]$ ]] || fail 'DROP_CACHES 和 DRY_RUN 只能是 0 或 1。'
if [ "$dry_run" -eq 0 ] && [ "$drop_caches" -eq 1 ]; then
    [ -w /proc/sys/vm/drop_caches ] || fail '默认每个 workload 前清页缓存，需要用 sudo bash 运行此脚本；不能清缓存时不会开始查询。'
fi
[ -x "$QUERY_DB_BENCH" ] || fail "查询程序未编译：$QUERY_DB_BENCH；先运行 build_hotrap_reads.sh。"
printf -v iteration_tag '%02d' "$iteration"
configs=()
# 开始前检查所有副本；未完成的预加载库绝不打开。
shopt -s nullglob
for workload in "${workloads[@]}"; do
    case "$workload" in 1.2|1.3|1.4) ;; *) fail "不支持的 Zipf：$workload" ;; esac
    matches=("$preload_root"/read_preload_zipf"$workload"_*_iter"$iteration_tag"/preload_config.env)
    [ "${#matches[@]}" -eq 1 ] || fail "zipf$workload 副本 $iteration_tag 的配置未找到或不唯一：$preload_root"
    config_file="${matches[0]}"
    [ -f "${config_file%/*}/PRELOAD_COMPLETE" ] || fail "预加载尚未完成：$config_file"
    # 此文件由预加载脚本生成，包含重开所需的原始参数。
    source "$config_file"
    [ "$ZIPF_A" = "$workload" ] && [ "$ITERATION" -eq "$iteration" ] || fail "配置中的 workload / iteration 不匹配：$config_file"
    [ -f "$DB_DIR/CURRENT" ] && [ -d "$TIER0_DIR" ] && [ -d "$TIER1_DIR" ] && [ -d "$RALT_DIR" ] || fail "预加载数据库路径缺失：$config_file"
    query_file="$workload_root/range_read_workload${workload}.csv"
    [ -r "$query_file" ] || fail "查询文件不可读：$query_file"
    configs+=("$config_file")
done
PID_IOSTAT=""
DB_PID=""
TEE_PID=""
LOG_FD=""
cleanup() {
    if [ -n "$DB_PID" ]; then
        kill "$DB_PID" 2>/dev/null || true
        wait "$DB_PID" 2>/dev/null || true
        DB_PID=""
    fi
    if [ -n "$LOG_FD" ]; then
        exec {LOG_FD}>&-
        LOG_FD=""
    fi
    if [ -n "$TEE_PID" ]; then
        wait "$TEE_PID" 2>/dev/null || true
        TEE_PID=""
    fi
    if [ -n "$PID_IOSTAT" ]; then
        kill "$PID_IOSTAT" 2>/dev/null || true
        wait "$PID_IOSTAT" 2>/dev/null || true
        PID_IOSTAT=""
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ulimit -n 800000 2>/dev/null || true
for config_file in "${configs[@]}"; do
    source "$config_file"
    query_file="$workload_root/range_read_workload${ZIPF_A}.csv"
    preload_case="$(basename "${config_file%/*}")"
    case_name="${preload_case/read_preload_/random_range_read_}_q${query_ops}_scan${scan_length}_offset${scan_offset}"
    case_name="${case_name}_wbs1mib_sst1mib_dropcache${drop_caches}"
    log_dir="$log_root/$case_name"
    log_file="$log_dir/HotRAP_$case_name.log"
    args=(
        --db="$DB_DIR" --db_paths="$DB_PATHS" --ralt_path="$RALT_DIR"
        --use_existing_db=true --readonly=false
        --max_hot_set_size="$MAX_HOT_SET_SIZE" --max_ralt_size="$MAX_RALT_SIZE"
        --compaction_style="$COMPACTION_STYLE" --ttl_seconds="$TTL_SECONDS"
        --level_compaction_dynamic_level_bytes="$LEVEL_COMPACTION_DYNAMIC_LEVEL_BYTES"
        --max_bytes_for_level_base="$MAX_BYTES_FOR_LEVEL_BASE"
        --target_file_size_base="$query_target_file_size"
        --num="$NUM_ENTRIES" --threads=1 --batch_size=1 --seed="$SEED"
        --key_size="$KEY_SIZE" --value_size="$VALUE_SIZE" --value_size_="$VALUE_SIZE"
        --benchmarks=scancsv,stats --YCSB_data_file="$query_file" --ycsb_num="$query_ops"
        --seek_nexts="$scan_length" --csv_scan_key_offset="$scan_offset"
        --bloom_bits="$BLOOM_BITS" --cache_size="$query_cache_size" --open_files="$query_open_files"
        --max_background_jobs="$MAX_BACKGROUND_JOBS"
        --max_background_compactions="$MAX_BACKGROUND_COMPACTIONS"
        --max_background_flushes="$MAX_BACKGROUND_FLUSHES"
        --compression_type="$COMPRESSION_TYPE" --write_buffer_size="$query_write_buffer_size"
        --stats_interval="$stats_interval_ops" --stats_per_interval=1 --histogram=1
        --mem_log_file="$log_dir/HotRAP_${case_name}_memory.log"
    )
    if [ "$dry_run" -eq 1 ]; then
        printf 'Preload config: %s\n' "$config_file"
        printf '%q ' "$QUERY_DB_BENCH" "${args[@]}"
        printf '\n'
        if [ "$drop_caches" -eq 1 ]; then
            echo 'DRY_RUN: 查询前将执行 sync 和 echo 1 > /proc/sys/vm/drop_caches；本次未执行。'
        else
            echo 'DRY_RUN: DROP_CACHES=0，本次配置为不清页缓存。'
        fi
        continue
    fi
    if [ -f "$log_dir/QUERY_COMPLETE" ]; then
        echo "查询已完成，跳过：$case_name"
        continue
    fi
    [ ! -e "$log_dir" ] || fail "已有未完成的查询日志：$log_dir；请归档日志并选择备用副本。"
    mkdir -p "$log_root"
    mkdir "$log_dir"
    cp "$config_file" "$log_dir/preload_config.env"
    {
        printf 'Preload config: %s\nQuery file: %s\nOperations: %s\nDROP_CACHES: %s\n' "$config_file" "$query_file" "$query_ops" "$drop_caches"
        printf 'Command: '; printf '%q ' "$QUERY_DB_BENCH" "${args[@]}"; printf '\n'
    } | tee "$log_file"
    # 与参考脚本一致：每个 workload 重开数据库之前先清 Linux 页缓存。
    if [ "$drop_caches" -eq 1 ]; then
        [ -w /proc/sys/vm/drop_caches ] || fail 'DROP_CACHES=1 需要 root 权限。'
        sync
        echo 1 > /proc/sys/vm/drop_caches
        printf 'Page cache cleared before query: sync + drop_caches=1 (%s)\n' "$(date -Is)" | tee -a "$log_file"
    fi
    if command -v iostat >/dev/null 2>&1; then
        iostat -d -x "${DEVICE_NAME:-nvme0n1}" "${PM_DEVICE_NAME:-pmem0}" "$iostat_interval_seconds" > "$log_dir/HotRAP_${case_name}_IOstats.log" &
        PID_IOSTAT=$!
    fi
    # 保存实际查询进程 PID；中断脚本时一并结束查询和采样。
    exec {LOG_FD}> >(tee -a "$log_file")
    TEE_PID=$!
    "$QUERY_DB_BENCH" "${args[@]}" >&"$LOG_FD" 2>&1 &
    DB_PID=$!
    set +e
    wait "$DB_PID"
    db_status=$?
    DB_PID=""
    exec {LOG_FD}>&-
    LOG_FD=""
    wait "$TEE_PID"
    tee_status=$?
    TEE_PID=""
    set -e
    cleanup
    [ "$db_status" -eq 0 ] && [ "$tee_status" -eq 0 ] || fail "查询失败，保留日志：$log_file"
    grep -Eq '^scancsv[[:space:]]*:.*[[:space:]]'"$query_ops"' operations;' "$log_file" || fail "未找到正确的操作数：$log_file"
    printf 'operations=%s\ncompleted_at=%s\n' "$query_ops" "$(date -Is)" > "$log_dir/QUERY_COMPLETE"
    echo "Query complete: $log_file"
done
