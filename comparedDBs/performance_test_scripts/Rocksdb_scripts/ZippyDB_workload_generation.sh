 db_dir="/mntdisk/rocks10B/zippyDB_test"
if [ ! -d "$db_dir" ]; then
    mkdir -p "$db_dir"
fi

# 如果目录不空，清空目录
if [ "$(ls -A "$db_dir")" ]; then
    rm -rf "${db_dir:?}/"*
fi
  
  ../../rocksdb/release/db_bench \
  --db="$db_dir" \
  --benchmarks="mixgraph" \
  --ZippyDB_FILE_PATH="/mnt/workloads/zippyDB_key_file.csv" \
  -use_direct_io_for_flush_and_compaction=true \
  -use_direct_reads=true \
  -cache_size=268435456 \
  -keyrange_dist_a=14.18 \
  -keyrange_dist_b=-2.917 \
  -keyrange_dist_c=0.0164 \
  -keyrange_dist_d=-0.08082 \
  -keyrange_num=30 \
  -value_k=0.2615 \
  -value_sigma=25.45 \
  -iter_k=2.517 \
  -iter_sigma=14.236 \
  -mix_get_ratio=0.85 \
  -mix_put_ratio=0.14 \
  -mix_seek_ratio=0.01 \
  -sine_mix_rate_interval_milliseconds=5000 \
  -sine_a=45000 \
  -sine_b=0.000023 \
  -sine_d=4500 \
  --perf_level=2 \
  -reads=420000000 \
  --compression_type=none \
  -num=50000000 \
  -key_size=48