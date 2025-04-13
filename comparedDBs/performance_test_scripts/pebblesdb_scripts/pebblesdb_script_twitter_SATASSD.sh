
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
    dir1="${i}B_Pebblesdb_Twitter_benchmarking"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 1380; do
            num_entries=600000000
            num_entries=1000000000
            stats_interva=10000000
            num_entries=100000000

            num_format=$(convert_to_billion_format $num_entries)

            for cluster_a in 37; do  #  1.2 

                for buffer_size in 67108864 ; do
                    max_file=40
                    buffer_size_mb=$((buffer_size / 1048576))
                    max_size_bytes=$((max_file * 1048576))
                    # log_file="Pebblesdb2_${num_format}_val_${value_size}_zipf${zipf_a}_1-30.log"
                    log_file="Pebblesdb_${num_format}_val_${value_size}_TwitterCluster${cluster_a}_${buffer_size_mb}MB.log"
                    data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                    memory_log_file="$(pwd)/Pebblesdb_memory_usage_key16val${value_size}_Twittercluster${cluster_a}_memory${buffer_size_mb}MiB.log"
                    # hot_files=$(printf "/home/jeff-wang/workloads/zipf${zipf_a}_top%%s_keys10B.csv," {1,5,10,15,20,25,30})
                    # hot_files=${hot_files%?} # 移除最后一个逗号

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    # 如果日志文件已存在，跳过
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping..."
                        continue
                    fi

                    # 创建相应的目录
                    db_dir="/mntdisk/pebbles10b/write_Cluster${cluster_a}_mem${wbs_mb}MB"
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 如果目录不空，清空目录
                    if [ "$(ls -A "$db_dir")" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

                    iostat -d 100 -x $DEVICE_NAME > Pebblesdb_SATASSD_${num_format}_val_${value_size}_Twittercluster${cluster_a}_mem_${buffer_size_mb}MB_IOstats.log &
                    PID_IOSTAT=$!
                        
                    cgexec -g memory:group200 ../../../pebblesdb/release/db_bench \
                --db=$db_dir \
                --num=$num_entries \
                --value_size=$value_size \
                --batch_size=1000 \
                --benchmarks=filltwitter,stats \
                --data_file=$data_file  \
                --bloom_bits=10 \
                --log=1  \
                --cache_size=8388608 \
                --open_files=1000 \
                --mem_log_file=$memory_log_file \
                --compression=0 \
                --stats_interval=$stats_interva \
                --histogram=1 \
                --write_buffer_size=$buffer_size \
                --max_file_size=$max_size_bytes   \
                --print_wa=true \
                &> >( tee $log_file) &  

                    # 保存 db_bench 的 PID 供监控使用
                    sleep 1

                    DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                    echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                    perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_TwitterCluster${cluster_a}_${buffer_size_mb}MB.txt" &
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



