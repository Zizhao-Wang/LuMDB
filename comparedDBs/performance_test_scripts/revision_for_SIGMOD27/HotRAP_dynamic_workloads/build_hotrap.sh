#!/usr/bin/env bash
# 编译带 fillzipf / fillcluster / YCSB A-F CSV 入口的 HotRAP db_bench。
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHARE_DIR="$(cd "$SCRIPT_DIR/../../../share" && pwd)"
JOBS="${JOBS:-8}"

if ! grep -q 'DEFINE_string(data_file_path,' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_bench.patch"
fi
if ! grep -q 'DEFINE_uint64(csv_scan_key_offset,' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_reads.patch"
fi

if ! grep -q 'void YCSBFCSV(' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_ycsb.patch"
fi

if ! grep -q 'void ExecuteTwitterDynamicCSV(' "$SHARE_DIR/hotrap/tools/db_bench_tool.cc"; then
    patch --batch --forward -d "$SHARE_DIR/hotrap" -p1 < "$SCRIPT_DIR/hotrap_csv_cluster_execute.patch"
fi

cmake -S "$SHARE_DIR/RALT" -B "$SHARE_DIR/RALT/build-experiments" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DROCKSDB_INCLUDE="$SHARE_DIR/hotrap/include" \
    -DROCKSDB_LIB="$SHARE_DIR/hotrap/build-experiments"
cmake --build "$SHARE_DIR/RALT/build-experiments" --target ralt -j "$JOBS"

cmake -S "$SHARE_DIR/hotrap" -B "$SHARE_DIR/hotrap/build-experiments" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DROCKSDB_BUILD_SHARED=OFF \
    -DWITH_TESTS=OFF -DWITH_BENCHMARK_TOOLS=ON -DWITH_GFLAGS=ON \
    -DWITH_TOOLS=OFF -DWITH_CORE_TOOLS=OFF \
    -DROCKSDB_INCLUDE_DIR="$SHARE_DIR/hotrap/include" \
    -DRALT_INCLUDE_DIR="$SHARE_DIR/RALT/include" \
    -DRALT_LIB_DIR="$SHARE_DIR/RALT/build-experiments"
cmake --build "$SHARE_DIR/hotrap/build-experiments" --target db_bench -j "$JOBS"
