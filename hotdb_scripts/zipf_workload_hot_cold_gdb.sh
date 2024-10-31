./config.sh

db_directory="/mnt/hotdb_test/zipf1.2_hotdefinition1"
rm -rf /mnt/hotdb_test/zipf1.2_hotdefinition1*
rm  /mnt/logs/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)
DEVICE_NAME="nvme0n1"


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
    dir1="${i}B_hotdb_zipf_hotkeydefinition"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 10))

            num_format=$(convert_to_billion_format $num_entries)

            for zipf_a in 1.1; do  # 1.2 1.3 1.4 1.5
                    percentages1=() # 1 5 10 15 20 25 30
                    No_hot_percentages=(10 20 30 40 50 60 70 80 90 100)

                    log_file="hotdb_${num_format}_val_${value_size}_zipf${zipf_a}.log"
                    data_file="/home/jeff-wang/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径
                    read_data_file="/home/jeff-wang/workloads/uniform_read_keys16_value128_1M.csv" 
                    
                    # hot_files=$(printf "/home/jeff-wang/workloads/zipf${zipf_a}_top%%s_keys10B.csv," {1,5,10,15,20,25,30})
                    # hot_files=${hot_files%?} # 移除最后一个逗号
                      
                    hot_files=""
                    for percent in "${percentages1[@]}"; do
                        if [[ -z "$hot_files" ]]; then
                            # 第一次迭代时，直接赋值
                            hot_files="/home/jeff-wang/workloads/zipf${zipf_a}_top${percent}_keys10.0B.csv"
                        else
                            # 后续迭代时，在现有字符串后面添加
                            hot_files="$hot_files,/home/jeff-wang/workloads/zipf${zipf_a}_top${percent}_keys10.0B.csv"
                        fi
                    done

                    echo "hot_files: $hot_files"
                    percentages_str="" #,5,10,15,20,25,30
                    for percent in "${percentages1[@]}"; do
                        if [[ -z "$percentages_str" ]]; then
                            # 第一次迭代时，直接赋值
                            percentages_str="${percent}"
                        else
                            # 后续迭代时，在现有字符串后面添加
                            percentages_str="$percentages_str,${percent}"
                            fi
                    done

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    # iostat -d 100 -x $DEVICE_NAME > leveldb2_${num_format}_val_${value_size}_zipf${zipf_a}_Nohot1-${no_hot}_IOstats.log &
                    # PID_IOSTAT=$!
                    
                    # 

                    # perf record -F 99 -a -g --

                    # taskset -c 0 perf record -F 99 -a -g
                    
                    # gdb --args  
                    
                     
                    gdb --args ../hotdb/release/db_bench \
                    --db=$db_directory \
                    --num=$num_entries \
                    --value_size=$value_size \
                    --batch_size=1000 \
                    --benchmarks=fillzipf,stats \
                    --hot_file=$hot_files \
                    --data_file=$data_file  \
                    --percentages=$percentages_str \
                    --logpath=/mnt/logs \
                    --bloom_bits=10 \
                    --log=1  \
                    --Read_data_file=$read_data_file  \
                    --cache_size=8388608 \
                    --open_files=40000 \
                    --compression=0 \
                    --stats_interval=$stats_interva \
                    --histogram=1 \
                    --max_file_size=2097152   \
                    --print_wa=true 
            done
        done
done



