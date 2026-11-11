#!/usr/bin/env bash
# 构建动态 cluster 混合执行程序；不替换已有预加载、查询和 YCSB 程序。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHARE_DIR="$(cd "$SCRIPT_DIR/../../../share" && pwd)"
if ! grep -q 'DEFINE_string(data_file_path,' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_bench.patch"
elif ! grep -q 'DEFINE_uint64(csv_scan_key_offset,' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_reads.patch"
fi
if ! grep -q 'void YCSBFCSV(' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_ycsb.patch"
fi

if ! grep -q 'void ExecuteTwitterDynamicCSV(' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_cluster_execute.patch"
fi

[ -f "$SHARE_DIR/hotrap/build-experiments/CMakeCache.txt" ] && \
[ -f "$SHARE_DIR/RALT/build-experiments/libralt.so" ] || {
    echo '请先运行 build_hotrap.sh 初始化 HotRAP/RALT 构建目录。' >&2
    exit 1
}
cmake -S "$SHARE_DIR/hotrap" -B "$SHARE_DIR/hotrap/build-experiments"
cmake --build "$SHARE_DIR/hotrap/build-experiments" --target db_bench_cluster_execute_csv -j "${JOBS:-4}"
