#!/usr/bin/env bash
# YCSB-F: 从备用预加载副本复制独立的 DB / PM / NVM / RALT，再执行 CSV。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QUERY_DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench_ycsb_csv}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
workload_letter=F
benchmark=ycsbf
ycsb_file="${YCSB_DATA_FILE:-/mnt/workloads/ycsb_f_workload.csv}"
requested_ycsb_ops="${YCSB_OPS:-auto}"
preload_root="${PRELOAD_ROOT:-$SCRIPT_DIR/1B_HotRAP_ReadPreload_zipf_PM_NVM_PaperRatio_open1000}"
source_iteration="${PRELOAD_ITERATION:-3}"
run_id="${RUN_ID:-run01}"
nvm_root="${YCSB_NVM_ROOT:-/mnt/nvm/hotrap1B_ycsb}"
pm_root="${YCSB_PM_ROOT:-/mnt/pmem0/hotrap1B_ycsb}"
log_root="${LOG_ROOT:-$SCRIPT_DIR/1B_HotRAP_YCSB_F_PM_NVM_PaperRatio_open1000}"
drop_caches="${DROP_CACHES:-1}"
dry_run="${DRY_RUN:-0}"
perf_enabled="${PERF:-1}"
scan_length="${SCAN_LENGTH:-20}"
scan_offset="${SCAN_KEY_OFFSET:-2000000}"
# 通用执行参数与已确认的 PebblesDB 查询脚本一致。
query_cache_size=8388608
query_open_files=1000
query_write_buffer_size=1048576
query_target_file_size=1048576
stats_interval_ops=100000000
iostat_interval_seconds=100
workloads=(1.2)  # 与已有 YCSB 脚本一致；命令行可选 1.2 1.3 1.4。
if [ "$#" -gt 0 ]; then workloads=("$@"); fi
fail() { echo "$*" >&2; exit 1; }
[[ "$source_iteration" =~ ^[1-9][0-9]*$ ]] || fail 'PRELOAD_ITERATION 必须是正整数。'
[[ "$requested_ycsb_ops" = auto || "$requested_ycsb_ops" =~ ^[1-9][0-9]*$ ]] || fail 'YCSB_OPS 必须是 auto 或正整数。' 
[[ "$run_id" =~ ^[a-zA-Z0-9_-]+$ ]] || fail 'RUN_ID 只能包含字母、数字、下划线和短横线。'
[[ "$scan_length" =~ ^(0|[1-9][0-9]*)$ && "$scan_offset" =~ ^(0|[1-9][0-9]*)$ ]] || fail 'SCAN_LENGTH 和 SCAN_KEY_OFFSET 必须是非负整数。'
[ "$scan_length" -le 2147483647 ] || fail 'SCAN_LENGTH 超出整数范围。'
[[ "$drop_caches" =~ ^[01]$ && "$dry_run" =~ ^[01]$ && "$perf_enabled" =~ ^[01]$ ]] || fail 'DROP_CACHES / DRY_RUN / PERF 只能为 0 或 1。'
[ -x "$QUERY_DB_BENCH" ] || fail "先编译 YCSB 程序：bash $SCRIPT_DIR/build_hotrap_ycsb.sh"
[ -r "$ycsb_file" ] || fail "YCSB CSV 不可读：$ycsb_file"
command -v python3 >/dev/null || fail '需要 python3 进行源数据库加锁和复制。'
# 在复制或打开数据库之前校验整个真实 CSV；只剔除明确混入数据区的 Key 表头。
# 原文件不改动。显式请求的操作数超出有效行数时直接报错。
input_summary=$(python3 "$SCRIPT_DIR/prepare_ycsb_csv.py" --input "$ycsb_file" --workload "$workload_letter" --operations "$requested_ycsb_ops" --tsv)
read -r ycsb_ops raw_ycsb_rows valid_ycsb_rows dropped_ycsb_rows <<< "$input_summary"
if [ "$dropped_ycsb_rows" -gt 0 ]; then
    echo "发现 $dropped_ycsb_rows 条数据区表头记录，将生成清洗副本：原始 $raw_ycsb_rows 条，有效 $valid_ycsb_rows 条，本次执行 $ycsb_ops 次。" >&2
fi
if [ "$dry_run" -eq 0 ] && [ "$drop_caches" -eq 1 ]; then
    [ -w /proc/sys/vm/drop_caches ] || fail '默认清页缓存，请使用 sudo bash 运行；不能清缓存时不会开始实验。'
fi
printf -v iteration_tag '%02d' "$source_iteration"
configs=()
shopt -s nullglob
for workload in "${workloads[@]}"; do
    case "$workload" in 1.2|1.3|1.4) ;; *) fail "不支持的 Zipf：$workload" ;; esac
    matches=("$preload_root"/read_preload_zipf"$workload"_*_iter"$iteration_tag"/preload_config.env)
    [ "${#matches[@]}" -eq 1 ] || fail "zipf$workload 副本 $iteration_tag 的配置缺失或不唯一。"
    config_file="${matches[0]}"
    [ -f "${config_file%/*}/PRELOAD_COMPLETE" ] || fail "预加载未完成：$config_file"
    source "$config_file"
    [ "$ZIPF_A" = "$workload" ] && [ "$ITERATION" -eq "$source_iteration" ] || fail "预加载配置不匹配：$config_file"
    [ -f "$DB_DIR/CURRENT" ] && [ -f "$DB_DIR/LOCK" ] && [ -d "$TIER0_DIR" ] && [ -d "$TIER1_DIR" ] && [ -d "$RALT_DIR" ] || fail "预加载路径缺失：$config_file"
    configs+=("$config_file")
done
DB_PID=""
PID_IOSTAT=""
PERF_PID=""
TEE_PID=""
LOG_FD=""
cleanup() {
    local pid
    if [ -n "$DB_PID" ]; then
        kill "$DB_PID" 2>/dev/null || true
        wait "$DB_PID" 2>/dev/null || true
        DB_PID=""
    fi
    if [ -n "$LOG_FD" ]; then exec {LOG_FD}>&-; LOG_FD=""; fi
    if [ -n "$TEE_PID" ]; then wait "$TEE_PID" 2>/dev/null || true; TEE_PID=""; fi
    for pid in "$PID_IOSTAT" "$PERF_PID"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    done
    PID_IOSTAT=""
    PERF_PID=""
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ulimit -n 800000 2>/dev/null || true
for config_file in "${configs[@]}"; do
    source "$config_file"
    source_db="$DB_DIR"
    source_tier0="$TIER0_DIR"
    source_tier1="$TIER1_DIR"
    source_ralt="$RALT_DIR"
    preload_case="$(basename "${config_file%/*}")"
    case_name="${preload_case/read_preload_/ycsb${workload_letter}_}_q${ycsb_ops}"
    if [ "$workload_letter" = E ]; then case_name="${case_name}_scan${scan_length}_offset${scan_offset}"; fi
    case_name="${case_name}_wbs1mib_sst1mib_dropcache${drop_caches}_csvchecked_${run_id}"
    db_dir="$nvm_root/db/$case_name"
    tier0_dir="$pm_root/tier0/$case_name"
    tier1_dir="$nvm_root/tier1/$case_name"
    ralt_dir="$pm_root/ralt/$case_name"
    db_paths="[{$tier0_dir,$TIER0_SIZE},{$tier1_dir,$TIER1_SIZE}]"
    log_dir="$log_root/$case_name"
    log_file="$log_dir/HotRAP_$case_name.log"
    prepared_file="$log_dir/prepared_ycsb_${workload_letter}.csv"
    args=(
        --db="$db_dir" --db_paths="$db_paths" --ralt_path="$ralt_dir"
        --use_existing_db=true --readonly=false
        --max_hot_set_size="$MAX_HOT_SET_SIZE" --max_ralt_size="$MAX_RALT_SIZE"
        --compaction_style="$COMPACTION_STYLE" --ttl_seconds="$TTL_SECONDS"
        --level_compaction_dynamic_level_bytes="$LEVEL_COMPACTION_DYNAMIC_LEVEL_BYTES"
        --max_bytes_for_level_base="$MAX_BYTES_FOR_LEVEL_BASE" --target_file_size_base="$query_target_file_size"
        --num="$NUM_ENTRIES" --threads=1 --batch_size=1 --seed="$SEED"
        --key_size="$KEY_SIZE" --value_size="$VALUE_SIZE" --value_size_="$VALUE_SIZE"
        --benchmarks="$benchmark,stats" --YCSB_data_file="$prepared_file" --ycsb_num="$ycsb_ops"
        --bloom_bits="$BLOOM_BITS" --cache_size="$query_cache_size" --open_files="$query_open_files"
        --max_background_jobs="$MAX_BACKGROUND_JOBS" --max_background_compactions="$MAX_BACKGROUND_COMPACTIONS"
        --max_background_flushes="$MAX_BACKGROUND_FLUSHES" --compression_type="$COMPRESSION_TYPE"
        --write_buffer_size="$query_write_buffer_size" --stats_interval="$stats_interval_ops" --stats_per_interval=1
        --histogram=1 --mem_log_file="$log_dir/HotRAP_${case_name}_memory.log"
    )
    if [ "$workload_letter" = E ]; then args+=(--seek_nexts="$scan_length" --csv_scan_key_offset="$scan_offset"); fi
    if [ "$dry_run" -eq 1 ]; then
        printf 'Source preload: %s\nCopy DB: %s -> %s\nCopy tier0: %s -> %s\nCopy tier1: %s -> %s\nCopy RALT: %s -> %s\n' "$config_file" "$source_db" "$db_dir" "$source_tier0" "$tier0_dir" "$source_tier1" "$tier1_dir" "$source_ralt" "$ralt_dir"
        printf '%q ' "$QUERY_DB_BENCH" "${args[@]}"; printf '\n'
        printf 'CSV validation: raw=%s valid=%s dropped=%s operations=%s\nPrepared CSV: %s -> %s\n' "$raw_ycsb_rows" "$valid_ycsb_rows" "$dropped_ycsb_rows" "$ycsb_ops" "$ycsb_file" "$prepared_file"
        echo "DRY_RUN: DROP_CACHES=$drop_caches; 已只读校验输入，不复制、不清缓存、不打开数据库。"
        continue
    fi
    if [ -f "$log_dir/QUERY_COMPLETE" ]; then echo "YCSB-$workload_letter 已完成，跳过：$case_name"; continue; fi
    for path in "$log_dir" "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir"; do
        [ ! -e "$path" ] || fail "已有实验目录，不覆盖：$path；新实验请设置 RUN_ID。"
    done
    mkdir -p "$log_root"
    mkdir "$log_dir"
    cp "$config_file" "$log_dir/preload_config.env"
    printf 'YCSB-%s source=%s operations=%s DROP_CACHES=%s\n' "$workload_letter" "$config_file" "$ycsb_ops" "$drop_caches" | tee "$log_file"
    prepared_summary=$(python3 "$SCRIPT_DIR/prepare_ycsb_csv.py" --input "$ycsb_file" --workload "$workload_letter" --operations "$requested_ycsb_ops" --output "$prepared_file" --report "$log_dir/workload_validation.json" --tsv)
    [ "$prepared_summary" = "$input_summary" ] || fail '准备期间输入记录数变化，停止实验。'
    printf 'CSV validation: raw=%s valid=%s dropped=%s operations=%s; details=workload_validation.json\n' "$raw_ycsb_rows" "$valid_ycsb_rows" "$dropped_ycsb_rows" "$ycsb_ops" | tee -a "$log_file"
    # fcntl 锁与 RocksDB 的数据库锁一致；数据库正在使用时拒绝复制。
    # 四个目录分别复制，不使用硬链接；跨目录的 path_id 映射顺序保持 tier0 / tier1。
    python3 - "$source_db" "$source_tier0" "$source_tier1" "$source_ralt" "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir" <<'COPY_PY' 2>&1 | tee -a "$log_file"
import fcntl, pathlib, subprocess, sys
sources = [pathlib.Path(x) for x in sys.argv[1:5]]
targets = [pathlib.Path(x) for x in sys.argv[5:9]]
with (sources[0] / 'LOCK').open('r+b') as lock:
    try:
        fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        sys.exit('源数据库正在使用，不能复制：' + str(sources[0]))
    for source, target in zip(sources, targets):
        if source.resolve() == target.resolve() or target.exists():
            sys.exit('复制目标必须是全新的独立目录：' + str(target))
    for source, target in zip(sources, targets):
        target.parent.mkdir(parents=True, exist_ok=True)
        target.mkdir()
        subprocess.run(['cp', '-a', '--reflink=auto', '--', str(source) + '/.', str(target) + '/'], check=True)
        print('Copied:', source, '->', target, flush=True)
COPY_PY
    [ -f "$db_dir/CURRENT" ] || fail '复制后找不到 CURRENT，停止实验。'
    {
        cat "$config_file"
        printf '%s=%q\n' DB_BENCH "$QUERY_DB_BENCH" DB_DIR "$db_dir" DB_PATHS "$db_paths" TIER0_DIR "$tier0_dir" TIER1_DIR "$tier1_dir" RALT_DIR "$ralt_dir" \
            WRITE_BUFFER_SIZE "$query_write_buffer_size" TARGET_FILE_SIZE_BASE "$query_target_file_size" CACHE_SIZE "$query_cache_size" OPEN_FILES "$query_open_files" \
            SOURCE_CONFIG "$config_file" YCSB_WORKLOAD "$workload_letter" YCSB_DATA_FILE "$prepared_file" ORIGINAL_YCSB_DATA_FILE "$ycsb_file" RAW_YCSB_ROWS "$raw_ycsb_rows" VALID_YCSB_ROWS "$valid_ycsb_rows" DROPPED_YCSB_ROWS "$dropped_ycsb_rows" YCSB_OPS "$ycsb_ops" DROP_CACHES "$drop_caches"
    } > "$log_dir/run_config.env"
    date -Is > "$log_dir/COPY_COMPLETE"
    { printf 'Command: '; printf '%q ' "$QUERY_DB_BENCH" "${args[@]}"; printf '\n'; } | tee -a "$log_file"
    if [ "$drop_caches" -eq 1 ]; then
        sync
        echo 1 > /proc/sys/vm/drop_caches
        printf 'Page cache cleared before query: sync + drop_caches=1 (%s)\n' "$(date -Is)" | tee -a "$log_file"
    fi
    if command -v iostat >/dev/null 2>&1; then
        iostat -d -x "${DEVICE_NAME:-nvme0n1}" "${PM_DEVICE_NAME:-pmem0}" "$iostat_interval_seconds" > "$log_dir/HotRAP_${case_name}_IOstats.log" &
        PID_IOSTAT=$!
    fi
    date -Is > "$log_dir/QUERY_STARTED"
    exec {LOG_FD}> >(tee -a "$log_file")
    TEE_PID=$!
    "$QUERY_DB_BENCH" "${args[@]}" >&"$LOG_FD" 2>&1 &
    DB_PID=$!
    if [ "$perf_enabled" -eq 1 ] && command -v perf >/dev/null 2>&1; then
        perf stat -p "$DB_PID" -o "$log_dir/HotRAP_${case_name}_perf_stat.txt" 2> "$log_dir/perf_stderr.log" &
        PERF_PID=$!
    fi
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
    [ "$db_status" -eq 0 ] && [ "$tee_status" -eq 0 ] || fail "YCSB-$workload_letter 失败，保留工作库和日志：$log_file"
    grep -Eq "^${benchmark}[[:space:]]*:.*[[:space:]]${ycsb_ops} operations;" "$log_file" || fail "未找到正确的操作数：$log_file"
    printf 'operations=%s\ncompleted_at=%s\n' "$ycsb_ops" "$(date -Is)" > "$log_dir/QUERY_COMPLETE"
    echo "YCSB-$workload_letter complete: $log_file"
done
