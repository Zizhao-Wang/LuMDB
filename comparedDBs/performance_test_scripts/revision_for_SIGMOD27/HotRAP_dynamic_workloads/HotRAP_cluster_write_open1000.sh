#!/usr/bin/env bash
# 按 LevelDB 写入脚本组织负载；直接从 data_file_path 读取并写入 HotRAP。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_BENCH="${DB_BENCH:-$SCRIPT_DIR/../../../share/hotrap/build-experiments/db_bench}"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../../share/RALT/build-experiments${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$SCRIPT_DIR" || exit 1


# 按论文 §4.1 设置 FD:SD=1:10，初始热集/RALT 为 FD 的 50%/15%。
# 同负载 RocksDB 日志最终 SST 约 16.08 GiB，取 16.5 GiB 总预算。
# 使用数据库存量估算容量，不使用包含重复更新的累计写入量。
# 快层 tier0 和 RALT 放在 PM，慢层 tier1 放在 NVM。容量单位为字节。
tier0_size=$((1536 * 1024 * 1024))
tier1_size=$((tier0_size * 10))
max_hot_set_size=$((tier0_size * 50 / 100))
max_ralt_size=$((tier0_size * 15 / 100))
# 默认 256 MiB 基数会让此规模的 PM 只有 L0/L1，无法调节快层倍率。
# 使用 64 MiB 基数，使 AutoTuner 能通过 L2 分配剩余 PM 数据预算。
level_base_bytes=$((64 * 1024 * 1024))

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

log_dir="HotRAP_Dynamic_TwitterCluster_PM_NVM_PaperRatio_FD1536MiB_SD15GiB_LBase64MiB_BG6_F1_workloads_open1000"

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 1038; do
        ops_per_workload=100000000
        clusters=(  40 49)
        data_files=()
        for cluster in "${clusters[@]}"; do
            data_files+=("/mnt/workloads/second_cluster${cluster}.sort")
        done

        data_file=$(IFS=,; echo "${data_files[*]}")
        cluster_tag=$(IFS=_; echo "${clusters[*]}")
        workload_count=${#clusters[@]}
        total_num_entries=$((ops_per_workload * workload_count))
        stats_interval_ops=10000000
        expected_report_count=$((total_num_entries / stats_interval_ops))
        num_format=$(convert_to_billion_format "$total_num_entries")

        for wbs_mb in "$MEM"; do
            wbs_bytes=$((wbs_mb * 1024 * 1024))
            max_file_size_bytes=$wbs_bytes

            log_file="HotRAP_write_${num_format}_val${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}.log"
            memory_log_file="$(pwd)/HotRAP_memory_usage_write_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_cluster${cluster_tag}.log"

            if [ -f "$log_file" ]; then
                echo "Log file $log_file already exists. Skipping..."
                continue
            fi

            db_dir="/mnt/nvm/hotrap10B/db/twitter_workloads_cluster${cluster_tag}_write_mem${wbs_mb}MB_pm_nvm_paperratio_fd1536mib_sd15gib_lbase64mib_bg6_f1"
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
            echo "Running HotRAP twitter multi-workload write with parameters:"
            echo "  base_num               = $base_num"
            echo "  ops_per_workload       = $ops_per_workload"
            echo "  workload_count         = $workload_count"
            echo "  total_num              = $total_num_entries"
            echo "  value_size             = $value_size"
            echo "  stats_interval         = $stats_interval_ops"
            echo "  expected_stats_reports = $expected_report_count"
            echo "  num_format             = $num_format"
            echo "  cluster_seq            = ${clusters[*]}"
            echo "  data_file              = $data_file"
            echo "  db_dir                 = $db_dir"
            echo "  tier0_dir (PM)         = $tier0_dir"
            echo "  tier1_dir (NVM)        = $tier1_dir"
            echo "  ralt_dir (PM)          = $ralt_dir"
            echo "  tier0_size             = $tier0_size bytes (1.5 GiB)"
            echo "  tier1_size             = $tier1_size bytes (15 GiB)"
            echo "  hot_set_limit          = $max_hot_set_size bytes (50% of FD)"
            echo "  ralt_limit             = $max_ralt_size bytes (15% of FD)"
            echo "  max_bytes_level_base   = $level_base_bytes bytes (64 MiB)"
            echo "  background             = 6 compactions, 1 flush"
            echo "  wbs_bytes              = $wbs_bytes (${wbs_mb}MB)"
            echo "  max_file_size_bytes    = $max_file_size_bytes"
            echo "=================================================="

            iostat_log="HotRAP_write_${num_format}_val_${value_size}_mem${wbs_mb}MB_cluster${cluster_tag}_IOstats.log"
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
                --num="$total_num_entries" \
                --value_size="$value_size" \
                --value_size_="$value_size" \
                --batch_size=1 \
                --benchmarks=fillcluster,stats \
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
