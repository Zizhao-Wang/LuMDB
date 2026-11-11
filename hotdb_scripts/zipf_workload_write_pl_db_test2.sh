
# ./config.sh

sudo bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'

BASE_VALUE_SIZE=128
billion=1000000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)
DEVICE_NAME="nvme0n1"
Mem=64
partition=9
hot_identification=2

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
    dir1="${i}B_LuMDB_Sep_skewness_sentivity_study"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            disk_prefix="/mnt/db_test2"
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 1000))
            num_format=$(convert_to_billion_format $num_entries)
            num_entries=100000000    

            for zipf_a in 1.15 ; do  # 1.2 1.3 1.4 1.5
                for min_partition in 48 ; do

                log_file="LuMDB_Write_${num_format}_val_${value_size}_mem${Mem}MiB_zipf${zipf_a}.log"
                reads_data_file="/mnt/workloads/zipf${zipf_a}_random_select_1000_read_keys.csv"
                data_file="/mnt/workloads/zipf${zipf_a}_keys1.0B.csv" # 构建数据文件路径

                memory_log_file="$(pwd)/LuMDB_memoryusage_${num_format}_key16_val${value_size}_wbs${wbs_mb}MB_zipf${zipf_a}.log"
                
                db_directory="${disk_prefix}/LuM10B/cold/write_mem${Mem}MiB_hot${hot_identification}_zipf${zipf_a}"
                hotdb_directory="${disk_prefix}/LuM10B//hot/write_mem${Mem}MiB_hot${hot_identification}_zipf${zipf_a}"

                if [ "$(ls -A $db_directory)" ]; then
                    rm -rf "${db_directory:?}/"*
                fi

                if [ ! -d "$db_directory" ]; then
                    mkdir -p "$db_directory"
                fi

                if [ "$(ls -A $hotdb_directory)" ]; then
                    rm -rf "${hotdb_directory:?}/"*
                fi

                if [ ! -d "$hotdb_directory" ]; then
                    mkdir -p "$hotdb_directory"
                fi

                # 如果日志文件存在，则跳过当前迭代
                # if [ -f "$log_file" ]; then
                #     echo "Log file $log_file already exists. Skipping this iteration."
                #     continue
                # fi

                min_partition_bytes=$((min_partition * 1024 * 1024))

                echo "base_num: $base_num"
                echo "num_entries: $num_entries"
                echo "value_size:$value_size"
                echo "stats_interval: $stats_interva"
                echo "$num_format"

                    
                ../../hotdb/release/db_bench \
                --db=$db_directory \
                --num=$num_entries \
                --value_size=$value_size \
                --batch_size=1000 \
                --hotdb=$hotdb_directory \
                --benchmarks=fillzipf2,stats \
                --data_file=$data_file  \
                --logpath=/mnt/logs \
                --bloom_bits=10 \
                --log=1  \
                --min_partition_size=$min_partition_bytes \
                --range_query_length=10 \
                --cache_size=8388608 \
                --use_pl_or_2ppl=true \
                --mem_log_file=$memory_log_file \
                --open_files=40000 \
                --use_existing_db=false \
                --reads=1000 \
                --compression=0 \
                --stats_interval=$stats_interva \
                --histogram=1 \
                --print_wa=true  

            done
                
            done
        done
done



