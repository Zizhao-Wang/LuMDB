
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)
DEVICE_NAME="pmem0"
memory_budget=4  # remeber to change cgroup

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


#!/bin/bash

# 函数：fix_ssd_paths
# 参数：
# $1: optane_path
# $2: qlc_path
fix_ssd_paths() {
    # 获取第一个路径的处理结果
    optane_path=$1
    t1=""
    for (( i=0; i<${#optane_path}; i++ )); do
        char="${optane_path:$i:1}"
        if [ "$char" != "/" ]; then
            t1="$t1$char"
        else
            t1="$t1\\/"
        fi
    done

    # 获取第二个路径的处理结果
    qlc_path=$2
    t2=""
    for (( i=0; i<${#qlc_path}; i++ )); do
        char="${qlc_path:$i:1}"
        if [ "$char" != "/" ]; then
            t2="$t2$char"
        else
            t2="$t2\\/"
        fi
    done

    # 返回处理后的路径
    echo "$t1 $t2"
}

# 函数：compile_src_changes
# 参数：
# $1: exp_cfg JSON 配置文件路径
compile_src_changes() {
    echo "---------Start Source Changes---------- $code $ssd $blockcache_size"
    # Parameters:
    # $1 - prismdb_src_path
    # $2 - optane_prism_path
    # $3 - qlc_prism_path
    # $4 - optane_ratio
    # $5 - pop_cache_size
    # $6 - pop_cache_thresh
    # $7 - db_size
    # $8 - key_size
    # $9 - value_size
    # ${10} - num_partitions

    # Define the paths
    local prismdb_src_path="$1"
    local optane_prism_path="$2"
    local qlc_prism_path="$3"
    local optane_ratio="$4"
    local pop_cache_size="$5"
    local pop_cache_thresh="$6"
    local db_size="$7"
    local key_size="$8"
    local value_size="$9"
    local num_partitions="${10}"
    
    # 获取 optane_prism_path 和 qlc_prism_path
    # 你可能需要从配置文件中读取它们，或者在外部定义它们
    # 示例： optane_prism_path="/path/to/optane"
    # 示例： qlc_prism_path="/path/to/qlc"
    
    # 获取 t1_str 和 t2_str
    result=$(fix_ssd_paths "$optane_prism_path" "$qlc_prism_path")
    t1_str=$(echo "$result" | awk '{print $1}')
    t2_str=$(echo "$result" | awk '{print $2}')
    
    # 替换代码中的路径和其他配置
    sed -i "s/.*#define PATH.*/#define PATH \"$t1_str\/slab-%d-%lu-%lu\"/g" "$prismdb_src_path/db/nvm/options.h"
    sed -i "s/.*    std::string flash_prism_path.*/    std::string flash_prism_path = \"$t2_str\/test-%d\";/g" "$prismdb_src_path/util/env_posix.cc"
    sed -i "s/.*float optaneThreshold.*/  float optaneThreshold = $optane_ratio;/g" "$prismdb_src_path/db/db_impl.h"
    sed -i "s/.*uint32_t popCacheSize.*/  uint32_t popCacheSize = $pop_cache_size;/g" "$prismdb_src_path/db/db_impl.h"
    sed -i "s/.*float popThreshold.*/  float popThreshold = $pop_cache_thresh;/g" "$prismdb_src_path/db/db_impl.h"
    sed -i "s/.*uint64_t numKeys.*/  uint64_t numKeys = $db_size;/g" "$prismdb_src_path/db/db_impl.h"
    sed -i "s/.*uint32_t maxKeySizeBytes.*/  uint32_t maxKeySizeBytes = $key_size;/g" "$prismdb_src_path/db/db_impl.h"
    sed -i "s/.*uint64_t numPartitions.*/  uint64_t numPartitions = $num_partitions;/g" "$prismdb_src_path/db/db_impl.h"
    
    # 计算 KV 大小并替换
    kv_size=$(($key_size + $value_size))
    align_to=64
    sed -i "s/.*uint32_t maxKVSizeBytes.*/  uint32_t maxKVSizeBytes = $(($kv_size + $align_to - ($kv_size % $align_to)));/g" "$prismdb_src_path/db/db_impl.h"
    
    sed -i "s/.*static int FLAGS_threadpool_num.*/static int FLAGS_threadpool_num = $num_partitions;/g" "$prismdb_src_path/benchmarks/db_bench.cc"


    # 切换目录并运行编译脚本
    pushd "$prismdb_src_path/scripts" > /dev/null
    ./make.sh "$prismdb_src_path" "$optane_prism_path" "$qlc_prism_path" > /home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot_removal/build.log 2>&1
    popd > /dev/null
    echo "---------Applied Source Changes---------- $code $ssd $blockcache_size"
}

get_disk_io_stats() {
    # 通过参数传递磁盘设备路径和日志文件路径
    local disk_device="$1"
    local disk_io_log_file="$2"

    # 检查磁盘设备路径和日志文件路径是否为空
    if [ -z "$disk_device" ]; then
        echo "Error: Disk device path must be provided."
        return 1
    fi
    
    if [ -z "$disk_io_log_file" ]; then
        echo "Error: Disk I/O log file path must be provided."
        return 1
    fi
    
    # 从 /proc/diskstats 获取磁盘 I/O 数据并筛选出与 disk_device 相关的行
    local stats=$(grep "$disk_device" /proc/diskstats)

    # 如果没有找到匹配的磁盘设备，输出错误信息并返回
    if [ -z "$stats" ]; then
        echo "Error: Disk device $disk_device not found in /proc/diskstats."
        return 1
    fi

    # 获取当前的时间戳
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    # 将时间戳和磁盘 I/O 数据写入指定的日志文件
    echo "$timestamp - $stats" >> "$disk_io_log_file"
}


for i in {10..10}; do
    base_num=$(($billion * $i))
    dir1="${i}B_prismdb_zipf"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$base_num 
            stats_interva=$((num_entries / 100))

            num_format=$(convert_to_billion_format $num_entries)

            for zipf_a in 1.3 ; do  #  1.2 
                    # buffer_size_mb=$((buffer_size / 1048576))
                    # log_file="prismdb2_${num_format}_val_${value_size}_zipf${zipf_a}_1-30.log"
                    log_file="prismdb_${num_format}_val_${value_size}_zipf${zipf_a}memory_budget${memory_budget}G.log"
                    data_file1="/home/jeff-wang/LuMDB/comparedDBs/prismdb-fix/kvexe-prismdb/10B_prismdb_twitter_workloads_odirect/latency_log_twitter_49_40G.log" # 构建数据文件路径
                    memory_log_file1="/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot/prismdb_memory_usage_${num_format}_key16_val${value_size}zipf${zipf_a}membudget${memory_budget}.log"
                    code_type="prismdb"
                    prismdb_srouce_path="/home/jeff-wang/LuMDB/comparedDBs/prismdb"
                    num_partitions=1
                    key_size1=16
                    loop_type="close"
                    kv_size=($key_size1+$value_size)

                    # 如果日志文件存在，则跳过当前迭代
                    # if [ -f "$log_file" ]; then
                    #     echo "Log file $log_file already exists. Skipping this iteration."
                    #     continue
                    # fi

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "key_size:$key_size1"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"        

                    # 创建相应的目录
                    optane_path1="/mnt/pmem0/prismdb10B_optane/write_kv${kv_size}_zipf${zipf_a}"
                    if [ ! -d "$optane_path1" ]; then
                        mkdir -p "$optane_path1"
                    fi
                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $optane_path1)" ]; then
                        rm -rf "${optane_path1:?}/"*
                    fi

                    flash_path1="/mntdisk/prism10B_ssd/memory${memory_budget}G_write_kv${kv_size}_zipf${zipf_a}"
                    if [ ! -d "$flash_path1" ]; then
                        mkdir -p "$flash_path1"
                    fi
                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $flash_path1)" ]; then
                        rm -rf "${flash_path1:?}/"*
                    fi
                    
                    db_key_space=20000000000

                    # echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

                    # iostat -d 100 -x $DEVICE_NAME > prismdb_HDD_${num_format}_val_${value_size}_zipf${zipf_a}_IOstats.log &
                    # PID_IOSTAT=$!
                    
                    # Example usage:
                    # compile_src_changes "/path/to/prismdb" "/path/to/optane" "/path/to/qlc" "prismdb" 0.8 1024 0.9 1000000 16 4       
                    compile_src_changes $prismdb_srouce_path $optane_path1 $flash_path1 0.05 10000000000 0.7 $db_key_space $key_size1 $value_size $num_partitions

                    # disk_device="sdb"  # 你可以根据需求传递不同的设备
                    # disk_io_log_file="/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot_removal/prismdb_zipf${zipf_a}_optane_diskio_stats.log"  # 你可以根据需求指定日志文件路径

                    # echo "Getting disk I/O stats before starting the process..." >> "$disk_io_log_file"
                    # get_disk_io_stats "$disk_device" "$disk_io_log_file"
                    # cgexec -g memory:group1
                    # remeber to change cgroup
                      gdb --args ../../../prismdb/build/db_bench \
                        --data_file_path=$data_file1 \
                        --stats_interval=$stats_interva \
                        --benchmarks=fillzipf,stats \
                        --num=$num_entries \
                        --reads=0 \
                        --load_threads=$num_partitions \
                        --mem_log_file=$memory_log_file1 \
                        --key_size=$key_size1 \
                        --value_size=$value_size \
                        --pop_file="" \
                        --cache_size=0 \
                        --migration_logging=1 \
                        --migration_policy=2 \
                        --migration_rand_range_num=8 \
                        --migration_metric=1 \
                        --num=10000000000 \
                        --value_size=$value_size \
                        --threads=$num_partitions \
                        --open_loop=0 

                        # 保存 db_bench 的 PID 供监控使用
                        # sleep 1

                        # DB_BENCH_PID=$(pgrep -af "prismdb/build/db_bench --data_file_path=$data_file1" | grep -v 'sudo' | awk '{print $1}')
                        # echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                        # perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_zipf${zipf_a}_membudget${memory_budget}.txt" &
                        # PERF_PID=$!

                        # 启动一个子进程来监控 db_bench 进程的 IO 情况
                        # iotop -p $DB_BENCH_PID -a -b > "prismdb_${num_format}_val_${value_size}_zipf${zipf_a}_IOstats.log" &
                        # IOTOP_PID=$!

                        # iotop -p $DB_BENCH_PID -a -b prismdb_${num_format}_val_${value_size}_zipf${zipf_a}_IOstats.log

                        # wait $DB_BENCH_PID

                        # 在进程结束后获取磁盘 I/O 数据
                        # echo "Getting disk I/O stats after finishing the process..." >> "$disk_io_log_file"
                        # get_disk_io_stats "$disk_device" "$disk_io_log_file"

                        # # 结束 iostat 进程
                        # echo "Checking if iostat process with PID $PID_IOSTAT is still running..."
                        # ps -p $PID_IOSTAT
                        # if [ $? -eq 0 ]; then
                        #     echo "iostat process $PID_IOSTAT is still running, killing now..."
                        #     kill -9 $PID_IOSTAT
                        # if [ $? -eq 0 ]; then
                        #     echo "iostat process $PID_IOSTAT has been successfully killed."
                        # else
                        #     echo "Failed to kill iostat process $PID_IOSTAT."
                        # fi
                        # else
                        #     echo "iostat process $PID_IOSTAT is no longer running."
                        # fi
                done
        done
done



