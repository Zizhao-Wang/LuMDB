echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'

BASE_VALUE_SIZE=128
billion=1000000000
read_multiplier=0.002

benchmarks=("get_uniform" "get_zipf0.99"  ) # "getrange" 


for benchmark in "${benchmarks[@]}"; do
    for i in {2..2}; do
        base_num=$(($billion * $i))
        for value_size in 128 256 512 1024 2048 ; do  #  128 256 512 1024 2048  4096 
            db_path="$db_dir/hirdb_${value_size}_2B" 
            num_entries=$(echo "$base_num * $BASE_VALUE_SIZE / $value_size * $read_multiplier / 1" | bc)
            stats_interval=$((num_entries / 100))
            log_dir="$log_dir_pre/getval${value_size}/with_filter_cache"
            # data_dir="/home/jeff-wang/remixdb/data_files"
            data_dir="/mnt/data_files"

            # 检查目录是否存在
            if [ ! -d "$log_dir" ]; then
                echo "Directory $log_dir does not exist. Creating now."
                mkdir -p "$log_dir"
            else
                echo "Directory $log_dir already exists."
            fi
            
            echo "Benchmark: $benchmark"
            echo "base_num: $base_num"
            echo "num_entries: $num_entries"
            echo "value_size: $value_size"
            echo "stats_interval: $stats_interval"
            echo "db_dir: $db_dir"
            echo "log_dir_pre: $log_dir_pre"
            echo "DEVICE_NAME: $DEVICE_NAME"

            data_file="${data_dir}/Hirdbdata_${benchmark}_key16_val${value_size}.txt"
            log_file="${log_dir}/Hirdbdata_${benchmark}_key16_val${value_size}_wi_cache_filter.log"
            memory_log_file="${log_dir}/memory_usage_${benchmark}_key16_val${value_size}_wi_cache_filter.log"
            echo "Running $benchmark without range_query_length"

            sh -c 'echo 3 > /proc/sys/vm/drop_caches'
            echo "Caches dropped successfully."

            iostat -d 1 -x $DEVICE_NAME > $log_dir/Hirdbdata_${benchmark}${range_length}_key16_val${value_size}_IOstats.out &
            PID_IOSTAT=$!

            ../../../release/db_bench \
            --db=$db_path \
            --num=$num_entries \
            --value_size=$value_size \
            --batch_size=1000 \
            --data_file=$data_file \
            --mem_log_file=$memory_log_file \
            --benchmarks=pointread,stats \
            --range=$num_entries \
            --logpath=/mnt/logs \
            --use_existing_db=true \
            --bloom_bits=10 \
            --log=1  \
            --cache_size=8388608 \
            --open_files=40000 \
            --is_generate=true \
            --stats_interval=$stats_interval \
            --histogram=1 \
            --write_buffer_size=67108864 \
            --max_file_size=67108864   \
            --print_wa=true \
            &> >( tee $log_file) &

            # 保存 db_bench 的 PID 供监控使用
            sleep 1

            DB_BENCH_PID=$(pgrep -af 'db_bench' | grep -v 'sudo' | awk '{print $1}')
            echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

            echo "DB_BENCH_PID: $DB_BENCH_PID is still running..."

            # Start perf to monitor db_bench
            # sudo perf stat -F 99 -g -p $DB_BENCH_PID -o "${log_dir}/perf_data_${benchmark}_key16_val${value_size}.data" &
            perf stat -p $DB_BENCH_PID 2>&1 |tee "${log_dir}/perf_stat_${benchmark}_key16_val${value_size}_perf_stats.txt" &
            PERF_PID=$!

            # Wait for db_bench to complete
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
