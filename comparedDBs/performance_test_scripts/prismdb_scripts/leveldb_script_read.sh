
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)
DEVICE_NAME="nvme3n1"


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
    dir1="${i}B_leveldb_read_performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 1000))

            num_format=$(convert_to_billion_format $num_entries)
            num_entries=10000000

            for zipf_a in 1.5; do  #  1.2 
                    percentages1=() # 1 5 10 15 20 25 30
                    No_hot_percentages=(0) #10 20 30 40 50 60 70 80 90 100

                    for no_hot in "${No_hot_percentages[@]}"; do

                        for buffer_size in 1048576; do

                            buffer_size_mb=$((buffer_size / 1048576))
                            reads_data_file="/home/jeff-wang/workloads/uniform_read_keys16_value128_1Thousand_noexists.csv"
                            log_file="LevelDBRead_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}.log"
                            data_file="/home/jeff-wang/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径
                            memory_log_file="/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hot_removal/leveldb_memory_usage_${num_format}_key16_val${value_size}.log"

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

                            # 创建相应的目录
                            db_dir="/mnt/hotdb_test/level10B/read_mem${buffer_size_mb}_${zipf_a}"
                            iostat -d 100 -x $DEVICE_NAME > leveldb_HDD_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}_IOstats.log &
                            PID_IOSTAT=$!
                        
                            cgexec -g memory:group16 ../../../leveldb/release/db_bench \
                            --db=$db_dir \
                            --num=$num_entries \
                            --value_size=$value_size \
                            --batch_size=1000 \
                            --benchmarks=readrandom,stats \
                            --Read_data_file=$reads_data_file  \
                            --data_file=$data_file  \
                            --logpath=/mnt/logs \
                            --bloom_bits=10 \
                            --use_existing_db=true \
                            --log=1  \
                            --reads=1000 \
                            --cache_size=8388608 \
                            --mem_log_file=$memory_log_file \
                            --open_files=40000 \
                            --compression=0 \
                            --stats_interval=$stats_interva \
                            --histogram=1 \
                            --write_buffer_size=$buffer_size \
                            --max_file_size=$buffer_size   \
                            --print_wa=true \
                            &> >( tee $log_file) &  

                            # 保存 db_bench 的 PID 供监控使用
                            sleep 1

                            DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                            echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                            perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}.txt" &
                            PERF_PID=$!

                            wait $DB_BENCH_PID

                            # 结束 iostat 进程
                            echo "Checking if iostat process with PID $PID_IOSTAT is still running..."
                            ps -p $PID_IOSTAT
                            if [ $? -eq 0 ]; then
                                echo "iostat process $PID_IOSTAT is still running, killing now..."
                                kill -9 $PID_IOSTAT
                                if [ $? -eq 0 ]; then
                                    echo "iostat process $PID_IOSTAT has been successfully killed."
                                else
                                    echo "Failed to kill iostat process $PID_IOSTAT."
                                fi
                            else
                                echo "iostat process $PID_IOSTAT is no longer running."
                            fi

                        done
                    done
            done
        done
done


