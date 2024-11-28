
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

DEVICE_NAME="nvme1n1"
MEM=8

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

for i in {10..10}; do
    base_num=$(($billion * $i))
    dir1="${i}B_RocksDB_YCSB_performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 100))

            num_format=$(convert_to_billion_format $num_entries)
            num_entries=1000000000
            ycsb_num_entries=1000000
            ycsb_num_entries=500000

            for zipf_a in 1.2; do  # 1.2 1.3 1.4 1.5

                # log_file="leveldb2_${num_format}_val_${value_size}_zipf${zipf_a}_1-30.log"
                log_file="RocksDB_${num_format}_val${value_size}_mem${MEM}MB_zipf${zipf_a}_randomA.log"
                data_file="/home/jeff-wang/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径 
                memory_log_file="/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_memoryusage${MEM}MB_${num_format}_key16_val${value_size}_zipf${zipf_a}_mem${MEM}MiB.log"      
                ycsb_data_file="/home/jeff-wang/workloads/ycsb_a_workload.csv" # 构建数据文件路径
                ycsb_data_file="/home/jeff-wang/workloads/random_read_workload${zipf_a}.csv"


                # 创建相应的目录
                db_dir="/mnt/hotdb_test/rocks10B/ycsba_mem${MEM}MB_${zipf_a}_ierator_1"
                
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
                --benchmarks=ycsba,stats \
                --data_file_path=$data_file  \
                --use_existing_db=true \
                --bloom_bits=10 \
                --cache_size=8388608 \
                --open_files=80000 \
                --compression_ratio=0 \
                --YCSB_data_file=$ycsb_data_file \
                --stats_interval=$stats_interva \
                --stats_per_interval=$stats_interva \
                --histogram=1 \
                --write_buffer_size=1048576 \
                --mem_log_file=$memory_log_file \
                --target_file_size_base=1048576   \
                --compression_type=none \
                | tee $log_file
                    # &> >( tee $log_file) &  

                # # 保存 db_bench 的 PID 供监控使用
                # sleep 1

                # DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                # echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                # perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_zipf${zipf_a}_mem${MEM}MiB.txt" &
                # PERF_PID=$!

                # wait $DB_BENCH_PID

                # # 结束 iostat 进程
                # echo "Checking if iostat process with PID $PID_IOSTAT is still running..."
                # ps -p $PID_IOSTAT
                # if [ $? -eq 0 ]; then
                #     echo "iostat process $PID_IOSTAT is still running, killing now..."
                #     kill -9 $PID_IOSTAT
                #     if [ $? -eq 0 ]; then
                #         echo "iostat process $PID_IOSTAT has been successfully killed."
                #     else
                #         echo "Failed to kill iostat process $PID_IOSTAT."
                #     fi
                # else
                #     echo "iostat process $PID_IOSTAT is no longer running."
                # fi
            done
        done
done



