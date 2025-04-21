
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
MEM=1

convert_to_billion_format() {
    local num=$1
    local integer_part=$((num / billion))
    local decimal_part=$((num % billion))

    if ((decimal_part == 0)); then
        echo "${integer_part}B"
    else
        # Convert decimal part to the format of "x billionths"
        local formatted_decimal=$(echo "scale=9; $decimal_part / $billion" | bc | sed 's/0*$//' | sed 's/\.$//')
        # Extract only the fractional part after the decimal point
        formatted_decimal=$(echo $formatted_decimal | cut -d'.' -f2)
        echo "${integer_part}.${formatted_decimal}B"
    fi
}

#!/usr/bin/env bash

convert_to_billion_format() {
    # 这里是示例函数，你可以根据实际需要进行改动
    local num=$1
    if (( num >= 1000000000 )); then
        local billions=$((num / 1000000000))
        echo "${billions}B"
    else
        echo "${num}"
    fi
}

billion=1000000000
BASE_VALUE_SIZE=128
DEVICE_NAME="sdc"  # 你的设备名称
MEM=512   # 你的 MEM 大小（MB）

log_dir="RocksDB_TwitterCluster_benchmarking_Multithread"

for i in {10..10}; do
    base_num=$((billion * i))
    dir1="${i}B_${log_dir}"
    if [ ! -d "$dir1" ]; then
        mkdir "$dir1"
    fi
    cd "$dir1" || exit 1

    for value_size in 1038; do
        num_entries=$((base_num * BASE_VALUE_SIZE / value_size))
        stats_interva=$((num_entries / 100))
        num_format=$(convert_to_billion_format "$num_entries")
        num_entries=100000000
        ycsb_num_entries=1000000
        for cluster in 40; do

            # 只用一个循环，同时设置 write_buffer_size 和 target_file_size_base
            for wbs_mb in 64; do
                # 将 MB 转换为字节
                wbs_mbb=40
                maxfilesize=$((wbs_mbb * 1024 * 1024))
                wbs_bytes=$((wbs_mb * 1024 * 1024))
                # target_file_size_base 同样用这个值
                tfsb_bytes=$maxfilesize

                echo "=================================================="
                echo "Running test with parameters:"
                echo "  base_num       = $base_num"
                echo "  num_entries    = $num_entries"
                echo "  value_size     = $value_size"
                echo "  stats_interval = $stats_interva"
                echo "  num_format     = $num_format"
                echo "  wbs_bytes      = $wbs_bytes  (${wbs_mb}MB)"
                echo "  tfsb_bytes     = $tfsb_bytes (${wbs_mb}MB)"
                echo "=================================================="


                # log_file="leveldb2_${num_format}_val_${value_size}_zipf${zipf_a}_1-30.log"
                log_file="RocksDB_${num_format}_val${value_size}_mem${MEM}MB_zipf${zipf_a}_YCSBB.log"
                data_file="/home/jeff-wang/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径 
                memory_log_file="/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_memoryusage${MEM}MB_${num_format}_key16_val${value_size}_zipf${zipf_a}_mem${MEM}MiB.log"      
                ycsb_data_file="/mnt/nvm/second_cluster40.sort" # 构建数据文件路径
                db_dir="/mnt/nvm/rocks10B/twitter${cluster}_write_10B_mem${wbs_mb}MB"
                # 如果日志文件存在，则跳过当前迭代
                # if [ -f "$log_file" ]; then
                #     echo "Log file $log_file already exists. Skipping this iteration."
                #     continue
                # fi

                echo "base_num: $base_num"
                echo "num_entries: $num_entries"
                echo "value_size:$value_size"
                echo "stats_interval: $stats_interva"
                echo "$num_format"
                echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'
                iostat -d 100 -x $DEVICE_NAME > RocksDB_${num_format}_val_${value_size}_zipf${zipf_a}_mem${MEM}MiB_IOstats.log &
                PID_IOSTAT=$!
                        
                ../../../rocksdb/release/db_bench \
                --db=$db_dir \
                --num=$num_entries \
                --value_size_=$value_size \
                --batch_size=1 \
                --benchmarks=twitterexecute,stats \
                --data_file_path=$data_file  \
                --use_existing_db=true \
                --bloom_bits=10 \
                --cache_size=8388608 \
                --open_files=80000 \
                --compression_ratio=0 \
                --max_background_compactions=6 \
                --max_background_flushes=1 \
                --ycsb_num=$ycsb_num_entries \
                --YCSB_data_file=$ycsb_data_file \
                --stats_interval=$stats_interva \
                --stats_per_interval=$stats_interva \
                --histogram=1 \
                --write_buffer_size=$wbs_bytes \
                --mem_log_file=$memory_log_file \
                --target_file_size_base=1048576   \
                --compression_type=none 

            done  # end of wbs_mb loop
        done      # end of zipf_a loop
    done          # end of value_size loop
    cd ..
done




