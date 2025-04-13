
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)


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
    dir1="${i}B_leveldb_TwitterCluster_Test"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 1380; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=10000000
            num_entries=100000000
            ycsb_num_entries=1000000
            num_format=$(convert_to_billion_format $num_entries)

            for Cluster_a in 40; do  #  1.2 

                for buffer_size in 67108864; do
                    wbs_mbb=40
                    buffer_size_mb=$((buffer_size / 1048576))
                    maxfilesize=$((wbs_mbb * 1024 * 1024))
                    log_file="leveldb_${num_format}_val_${value_size}_Cluster${Cluster_a}_${buffer_size_mb}MB.log"
                    data_file="/mnt/nvm/second_cluster${Cluster_a}.sort"
                    memory_log_file="$(pwd)/leveldb_memory_usage_key16val${value_size}_Cluster${Cluster_a}_memory${buffer_size_mb}MiB.log"
                    ycsb_data_file="/mnt/nvm/second_cluster${Cluster_a}.sort"
                    ycsb_data_file="/mnt/nvm/cluster37_modified.sort" # 构建数据文件路径

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
                    db_dir="/mnt/nvm/level10B/twitter_workloads_${Cluster_a}_mem${wbs_mb}MB"

                    echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

                    iostat -d 100 -x $DEVICE_NAME > leveldb_SATASSD_${num_format}_val_${value_size}_Cluster${Cluster_a}_mem_${buffer_size_mb}MB_IOstats.log &
                    PID_IOSTAT=$!
                        
                    gdb --args  ../../../leveldb/release/db_bench \
                    --db=$db_dir \
                    --num=$num_entries \
                    --value_size=$value_size \
                    --batch_size=1000 \
                    --benchmarks=twitterexecute,stats \
                    --data_file=$data_file  \
                    --logpath=/mnt/logs \
                    --bloom_bits=10 \
                    --log=1  \
                    --cache_size=0 \
                    --mem_log_file=$memory_log_file \
                    --open_files=40000 \
                    --ycsb_num=$ycsb_num_entries \
                    --compression=0 \
                    --stats_interval=$stats_interva \
                    --histogram=1 \
                    --write_buffer_size=$buffer_size \
                    --YCSB_data_file=$ycsb_data_file \
                    --use_existing_db=true \
                    --max_file_size=$buffer_size   \
                    --print_wa=true 
                    


                done
            done
        done
done



