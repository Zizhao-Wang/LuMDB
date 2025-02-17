sudo rm -rf /mnt/nvm/level8B*
sudo rm  /mnt/logs/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=10000
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

for i in {9..9}; do
    base_num=$(($billion * $i))
    dir1="${i}B_leveldb_testing"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 100))

            num_format=$(convert_to_billion_format $num_entries)

            for zipf_a in 1.01 1.1 1.2 1.3 1.4 1.5; do
                    log_file="leveldb2_${num_format}_val_${value_size}_uniform.log"
                    data_file="/home/jeff-wang/workloads/etc_data_zipf${zipf_a}.csv" # 构建数据文件路径
                    hot_files=$(printf "/home/jeff-wang/workloads/zipf${zipf_a}_top%%s_keys10B.csv," {1,5,10,15,20,25,30})
                    hot_files=${hot_files%?} # 移除最后一个逗号
                    percentages="1,5,10,15,20,25,30"

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        cd ..
                        continue
                    fi

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    sudo ../../../leveldb/release/db_bench \
                    --db=/mnt/nvm/level8B \
                    --num=$num_entries \
                    --value_size=$value_size \
                    --batch_size=1000 \
                    --benchmarks=fillrandom,stats \
                    --data_file=$data_file  \
                    --hot_file=$hot_file \
                    --percentages=$percentages \
                    --logpath=/mnt/logs \
                    --bloom_bits=10 \
                    --log=1  \
                    --cache_size=8388608 \
                    --open_files=40000 \
                    --stats_interval=$stats_interva \
                    --histogram=1 \
                    --write_buffer_size=67108864 \
                    --max_file_size=67108864   \
                    --print_wa=true \
                    | tee $log_file  
            done
        done
done



