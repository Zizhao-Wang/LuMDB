#!/usr/bin/env bash
# 仿照 RocksDB_cluster_execute_open1000.sh：空库，35 -> 40 -> 37，真实混合读写。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench_cluster_execute_csv}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
clusters=(35 40 37)
if [ "$#" -gt 0 ]; then clusters=("$@"); fi
ops_per_workload="${OPS_PER_WORKLOAD:-100000000}"
value_size=1038
wbs_mib="${WBS_MIB:-64}"
stats_interval_ops="${STATS_INTERVAL_OPS:-10000000}"
# 同序列 RocksDB 的 SST 峰值 39.24 GiB；向上取整到 44 GiB，FD:SD = 1:10。
tier0_mib="${TIER0_MIB:-4096}"
level_base_mib="${LEVEL_BASE_MIB:-256}"
workload_root="${WORKLOAD_ROOT:-/mnt/workloads}"
nvm_root="${NVM_ROOT:-/mnt/nvm/hotrap_cluster_execute}"
pm_root="${PM_ROOT:-/mnt/pmem0/hotrap_cluster_execute}"
log_root="${LOG_ROOT:-$SCRIPT_DIR/HotRAP_Dynamic_TwitterCluster_execute_PM_NVM_PaperRatio_open1000}"
run_id="${RUN_ID:-run01}"
drop_caches="${DROP_CACHES:-1}"
dry_run="${DRY_RUN:-0}"
perf_enabled="${PERF:-1}"
fail() { echo "$*" >&2; exit 1; }
for number in "$ops_per_workload" "$wbs_mib" "$stats_interval_ops" "$tier0_mib" "$level_base_mib"; do
    [[ "$number" =~ ^[1-9][0-9]*$ ]] || fail '操作数、统计间隔与容量必须是正整数。'
done
[[ "$run_id" =~ ^[a-zA-Z0-9_-]+$ ]] || fail 'RUN_ID 只能包含字母、数字、短横线或下划线。'
[[ "$drop_caches" =~ ^[01]$ && "$dry_run" =~ ^[01]$ && "$perf_enabled" =~ ^[01]$ ]] || fail 'DROP_CACHES / DRY_RUN / PERF 必须为 0 或 1。'
[ "$tier0_mib" -ge $((12 * level_base_mib)) ] || fail '快层至少需要 12 倍 level base，保证 HotRAP 初始快层包含可调节的 L2。'
[ -x "$DB_BENCH" ] || fail "请先编译：bash $SCRIPT_DIR/build_hotrap_cluster_execute.sh"
command -v python3 >/dev/null || fail '需要 python3 校验输入格式。'
data_files=()
for cluster in "${clusters[@]}"; do
    [[ "$cluster" =~ ^[0-9]+$ ]] || fail "cluster 编号必须为数字：$cluster"
    path="$workload_root/second_cluster${cluster}.sort"
    [ -r "$path" ] || fail "无法读取：$path"
    data_files+=("$path")
done
# 原始 trace 为七列、无表头；抽查入口格式，后续由 db_bench 全程逐行校验。
python3 - "${data_files[@]}" <<'CHECK_PY'
import csv, sys
for path in sys.argv[1:]:
    valid = 0
    with open(path, newline='') as stream:
        for index, row in enumerate(csv.reader(stream)):
            if index >= 1000:
                break
            if (len(row) == 7 and row[1].isascii() and row[1].isdigit()
                    and int(row[1]) <= (1 << 64) - 1
                    and row[5].strip() in {'get', 'gets', 'set', 'add', 'cas', 'delete'}):
                valid += 1
    if not valid:
        sys.exit('前 1000 行未发现有效的七列 cluster 操作：' + path)
CHECK_PY
if [ "$dry_run" -eq 0 ] && [ "$drop_caches" -eq 1 ]; then
    [ -w /proc/sys/vm/drop_caches ] || fail '默认清页缓存，请使用 sudo bash 运行；失败时不会开始实验。'
fi
workload_count=${#clusters[@]}
total_entries=$((ops_per_workload * workload_count))
expected_report_count=$((total_entries / stats_interval_ops))
cluster_tag=$(IFS=_; echo "${clusters[*]}")
data_file=$(IFS=,; echo "${data_files[*]}")
wbs_bytes=$((wbs_mib * 1024 * 1024))
tier1_mib=$((tier0_mib * 10))
tier0_size=$((tier0_mib * 1024 * 1024))
tier1_size=$((tier1_mib * 1024 * 1024))
max_hot_set_size=$((tier0_size * 50 / 100))
max_ralt_size=$((tier0_size * 15 / 100))
level_base_bytes=$((level_base_mib * 1024 * 1024))
case_name="execute_cluster${cluster_tag}_ops${ops_per_workload}_val${value_size}_mem${wbs_mib}MB_fd${tier0_mib}mib_sd${tier1_mib}mib_lbase${level_base_mib}mib_bg6_f1_dropcache${drop_caches}_${run_id}"
log_dir="$log_root/$case_name"
log_file="$log_dir/HotRAP_execute_${total_entries}_val${value_size}_mem${wbs_mib}MB_cluster${cluster_tag}.log"
memory_log_file="$log_dir/HotRAP_memoryusage_execute_${total_entries}_key24_val${value_size}_wbs${wbs_mib}MB_cluster${cluster_tag}.log"
db_dir="$nvm_root/db/$case_name"
tier0_dir="$pm_root/tier0/$case_name"
tier1_dir="$nvm_root/tier1/$case_name"
ralt_dir="$pm_root/ralt/$case_name"
db_paths="[{$tier0_dir,$tier0_size},{$tier1_dir,$tier1_size}]"
args=(
    --db="$db_dir" --db_paths="$db_paths" --ralt_path="$ralt_dir"
    --max_hot_set_size="$max_hot_set_size" --max_ralt_size="$max_ralt_size"
    --compaction_style=0 --ttl_seconds=0 --level_compaction_dynamic_level_bytes=false
    --max_bytes_for_level_base="$level_base_bytes"
    --num="$total_entries" --threads=1 --key_size=24 --value_size="$value_size" --value_size_="$value_size"
    --batch_size=1 --benchmarks=twitterexecutedynamic,stats --use_existing_db=false
    --bloom_bits=10 --cache_size=8388608 --open_files=1000 --compression_ratio=0 --compression_type=none
    --max_background_jobs=7 --max_background_compactions=6 --max_background_flushes=1
    --YCSB_data_file="$data_file" --stats_interval="$stats_interval_ops" --stats_per_interval=1
    --histogram=1 --write_buffer_size="$wbs_bytes" --target_file_size_base="$wbs_bytes"
    --mem_log_file="$memory_log_file"
)
if [ "$dry_run" -eq 1 ]; then
    printf 'Clusters: %s\nOps per workload: %s\nTotal operations: %s\nExpected interval stats: %s\nPM / NVM: %s / %s MiB\n' "${clusters[*]}" "$ops_per_workload" "$total_entries" "$expected_report_count" "$tier0_mib" "$tier1_mib"
    printf '%q ' "$DB_BENCH" "${args[@]}"; printf '\n'
    echo "DRY_RUN: DROP_CACHES=$drop_caches；不创建目录、不清缓存、不打开数据库。"
    exit 0
fi
if [ -f "$log_dir/EXECUTE_COMPLETE" ]; then echo "已完成，跳过：$log_dir"; exit 0; fi
# 从空库开始；已有目录不自动删除，重跑用新的 RUN_ID。
for path in "$log_dir" "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir"; do
    [ ! -e "$path" ] || fail "目录已存在，不覆盖：$path；重跑请设置 RUN_ID=run02。"
done
DB_PID=""
PID_IOSTAT=""
PERF_PID=""
TEE_PID=""
LOG_FD=""
cleanup() {
    local pid
    if [ -n "$DB_PID" ]; then kill "$DB_PID" 2>/dev/null || true; wait "$DB_PID" 2>/dev/null || true; DB_PID=""; fi
    for pid in "$PID_IOSTAT" "$PERF_PID"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    done
    PID_IOSTAT=""
    PERF_PID=""
    if [ -n "$LOG_FD" ]; then exec {LOG_FD}>&-; LOG_FD=""; fi
    if [ -n "$TEE_PID" ]; then wait "$TEE_PID" 2>/dev/null || true; TEE_PID=""; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ulimit -n 800000 2>/dev/null || true
mkdir -p "$log_root" "$nvm_root/db" "$nvm_root/tier1" "$pm_root/tier0" "$pm_root/ralt"
mkdir "$log_dir"
mkdir "$db_dir" "$tier0_dir" "$tier1_dir" "$ralt_dir"
{
    printf 'Cluster sequence: %s\nOps per workload: %s\nTotal operations: %s\nStats interval: %s (expected %s reports)\n' "${clusters[*]}" "$ops_per_workload" "$total_entries" "$stats_interval_ops" "$expected_report_count"
    printf 'Tier0 PM: %s MiB\nTier1 NVM: %s MiB\nHot set: %s bytes\nRALT: %s bytes\nDROP_CACHES: %s\n' "$tier0_mib" "$tier1_mib" "$max_hot_set_size" "$max_ralt_size" "$drop_caches"
    printf 'Command: '; printf '%q ' "$DB_BENCH" "${args[@]}"; printf '\n'
} | tee "$log_file"
{
    printf '%s=%q\n' DB_BENCH "$DB_BENCH" DB_DIR "$db_dir" DB_PATHS "$db_paths" TIER0_DIR "$tier0_dir" TIER1_DIR "$tier1_dir" RALT_DIR "$ralt_dir" \
        TIER0_SIZE "$tier0_size" TIER1_SIZE "$tier1_size" MAX_HOT_SET_SIZE "$max_hot_set_size" MAX_RALT_SIZE "$max_ralt_size" \
        CLUSTERS "$cluster_tag" DATA_FILES "$data_file" OPS_PER_WORKLOAD "$ops_per_workload" NUM_ENTRIES "$total_entries" \
        KEY_SIZE 24 VALUE_SIZE "$value_size" WRITE_BUFFER_SIZE "$wbs_bytes" TARGET_FILE_SIZE_BASE "$wbs_bytes" \
        MAX_BYTES_FOR_LEVEL_BASE "$level_base_bytes" CACHE_SIZE 8388608 OPEN_FILES 1000 MAX_BACKGROUND_JOBS 7 MAX_BACKGROUND_COMPACTIONS 6 MAX_BACKGROUND_FLUSHES 1 \
        STATS_INTERVAL "$stats_interval_ops" EXPECTED_STATS_REPORTS "$expected_report_count" DROP_CACHES "$drop_caches"
} > "$log_dir/execute_config.env"
if [ "$drop_caches" -eq 1 ]; then
    sync
    echo 1 > /proc/sys/vm/drop_caches
    printf 'Page cache cleared: sync + drop_caches=1 (%s)\n' "$(date -Is)" | tee -a "$log_file"
fi
if command -v iostat >/dev/null 2>&1; then
    iostat -d 100 -x "${DEVICE_NAME:-nvme0n1}" "${PM_DEVICE_NAME:-pmem0}" > "$log_dir/HotRAP_execute_${total_entries}_val${value_size}_mem${wbs_mib}MB_cluster${cluster_tag}_IOstats.log" &
    PID_IOSTAT=$!
fi
date -Is > "$log_dir/EXECUTE_STARTED"
exec {LOG_FD}> >(tee -a "$log_file")
TEE_PID=$!
"$DB_BENCH" "${args[@]}" >&"$LOG_FD" 2>&1 &
DB_PID=$!
if [ "$perf_enabled" -eq 1 ] && command -v perf >/dev/null 2>&1; then
    perf stat -p "$DB_PID" -o "$log_dir/HotRAP_execute_${total_entries}_cluster${cluster_tag}_perf_stat.txt" 2> "$log_dir/perf_stderr.log" &
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
[ "$db_status" -eq 0 ] && [ "$tee_status" -eq 0 ] || fail "执行失败，保留工作库和日志：$log_file"
grep -Eq "^twitterexecutedynamic[[:space:]]*:.*[[:space:]]${total_entries} operations;" "$log_file" || fail '最终操作数不正确，不标记完成。'
actual_files=$(grep -c '^Finished workload \[' "$log_file" || true)
actual_reports=$(grep -c '^num-running-flushes:' "$log_file" || true)
[ "$actual_files" -eq "$workload_count" ] || fail '文件完成数量不正确，不标记完成。'
[ "$actual_reports" -eq "$expected_report_count" ] || fail "累计 stats 数量不正确：实际 $actual_reports，期望 $expected_report_count。"
printf 'operations=%s\nworkloads=%s\ninterval_stats=%s\ncompleted_at=%s\n' "$total_entries" "$workload_count" "$actual_reports" "$(date -Is)" > "$log_dir/EXECUTE_COMPLETE"
echo "Cluster execute complete: $log_file"
