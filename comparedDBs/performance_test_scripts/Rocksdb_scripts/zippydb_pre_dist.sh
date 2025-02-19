 db_dir="/mntdisk/rocks10B/zippyDB_test"
if [ ! -d "$db_dir" ]; then
    mkdir -p "$db_dir"
fi

# 如果目录不空，清空目录
if [ "$(ls -A "$db_dir")" ]; then
    rm -rf "${db_dir:?}/"*
fi
  
    ../../rocksdb/release/db_bench \
    --benchmarks="mixgraph" \
    --use_direct_io_for_flush_and_compaction=true \
    -use_direct_reads=true -cache_size=268435456 \
    -key_dist_a=0.002312 \
    -key_dist_b=0.3467 \
    -keyrange_dist_a=14.18 \
    -keyrange_dist_b=-2.917 \
    -keyrange_dist_c=0.0164 \
    -keyrange_dist_d=-0.08082
    -keyrange_num=30 
    -value_k=0.2615 
    -value_sigma=25.45
    -iter_k=2.517 
-iter_sigma=14.236 
-mix_get_ratio=0.83
-mix_put_ratio=0.14 
-mix_seek_ratio=0.03 
-sine_mix_rate_interval_milliseconds=5000 
-sine_a=1000
-sine_b=0.000073 
-sine_d=4500 
-perf_level=2 
-reads=420000000 
-num=50000000 
-key_size=48 
-db=./<Directory of Generated Database with 50 million KV-pairs>
-use_existing_db=true