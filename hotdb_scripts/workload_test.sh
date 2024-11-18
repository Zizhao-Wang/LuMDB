BASE_VALUE_SIZE=128
billion=1000000

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
    dir1="hotdb_${i}B"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 10))
            key_size=16
            num_format=$(convert_to_billion_format $num_entries)

            for zipf_a in 1.02 ; do  #1.2 1.3 1.4
                    percentages1=(1 ) #5 10 15 20 25 30

                    log_file="hotdb1.0M_hotkeydefinition_key${key_size}_val${value_size}_zipf${zipf_a}.log"
                    data_file="/home/jeff-wang/hotdb_workloads_test/zipf${zipf_a}_keys${key_size}_value${value_size}_1.0M.csv" # 构建数据文件路径
                    # hot_files=$(printf "/home/jeff-wang/workloads/zipf${zipf_a}_top%%s_keys10B.csv," {1,5,10,15,20,25,30})
                    # hot_files=${hot_files%?} # 移除最后一个逗号

                    # 如果日志文件存在
                    if [ -f "$log_file" ]; then
                        # 如果文件不为空
                        if [ -s "$log_file" ]; then
                            echo "Log file $log_file already exists and is not empty. Skipping this iteration."
                            continue
                        else
                            # 文件存在但为空，删除文件
                            echo "Log file $log_file is empty. Deleting file."
                            rm "$log_file"
                        fi
                    fi


                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    sudo ../../hotdb/release/db_bench \
                    --db=/mnt/nvm/level8B \
                    --num=$num_entries \
                    --value_size=$value_size \
                    --batch_size=1000 \
                    --benchmarks=fillrandom,stats \
                    --data_file=$data_file  \
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



