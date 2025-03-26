#!/bin/bash
# run.sh - 运行 rocksdb-kvexe 可执行文件

# 可执行文件路径（假设当前目录下有 rocksdb-kvexe）
EXECUTABLE="./rocksdb-kvexe"
CMAKE_CACHE_FILE="./CMakeCache.txt"

# 检查可执行文件和 CMakeCache.txt 是否存在，并删除
if [ -f "$EXECUTABLE" ]; then
    echo "删除旧的可执行文件: $EXECUTABLE"
    rm "$EXECUTABLE"
fi

if [ -f "$CMAKE_CACHE_FILE" ]; then
    echo "删除 CMakeCache.txt"
    rm "$CMAKE_CACHE_FILE"
fi

# 运行 cmake 配置并编译
echo "运行 cmake 配置和编译"
cmake ..
make



# 检查可执行文件是否成功生成
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误：无法生成 $EXECUTABLE"
    exit 1
fi

dir1="10B_prismdb_twitter_workloads"
if [ ! -d "$dir1" ]; then
    mkdir $dir1
fi

log_dir="/home/jeff-wang/LuMDB/comparedDBs/prismdb-fix/kvexe-prismdb/${dir1}"

pm_size=50


# 遍历 data_file_path 中的每个文件
for cluster_a in 40; do 

    # 定义性能监控日志文件路径，包含 cluster_a
    pm_monitor_log_file="${log_dir}/pcm_memory_log_twitter_${cluster_a}_${pm_size}G.log"
    disk_monitor_log_file="${log_dir}/disk_io_log_twitter_${cluster_a}_${pm_size}G.log"
    memory_log_file="${log_dir}/memory_log_twitter_${cluster_a}_${pm_size}G.log"
    lat_log_file="${log_dir}/latency_log_twitter_${cluster_a}_${pm_size}G.log"

    # 如果日志文件存在，则跳过当前迭代
    if [ -f "$memory_log_file" ]; then
        echo "Log file $memory_log_file already exists. Skipping this iteration."
        continue
    fi

    # 删除已存在的文件
    if [ -f "$pm_monitor_log_file" ]; then
        echo "File $pm_monitor_log_file exists. Deleting it."
        rm "$pm_monitor_log_file"
    fi

    # 创建一个新的文件
    touch "$pm_monitor_log_file"
    echo "New pm log file created: $pm_monitor_log_file"


    # 删除已存在的文件
    if [ -f "$lat_log_file" ]; then
        echo "File $lat_log_file exists. Deleting it."
        rm "$lat_log_file"
    fi

    # 创建一个新的文件
    touch "$lat_log_file"
    echo "New latency log file created: $lat_log_file"

    # 删除已存在的文件
    if [ -f "$disk_monitor_log_file" ]; then
        echo "File $disk_monitor_log_file exists. Deleting it."
        rm "$disk_monitor_log_file"
    fi

    # 创建一个新的文件
    touch "$disk_monitor_log_file"
    echo "New disk monitor log file created: $disk_monitor_log_file"

    # 启动 pcm-memory 性能监控，后台运行并将输出写入 CSV 文件
    echo "启动 PCM 内存性能监控，输出到 $pm_monitor_log_file"
    nohup /home/jeff-wang/pcm/build/bin/pcm-memory -pmm 1 -csv=$pm_monitor_log_file & 
    PCM_PID=$!

    # 启动磁盘 IO 监控
    echo "启动磁盘 IO 监控，输出到 $disk_monitor_log_file"
    nohup iostat -d 1 -x sdd > $disk_monitor_log_file & 
    DISK_PID=$!

    # 检查是否成功启动 PCM 和磁盘监控工具
    if [ -z "$PCM_PID" ] || [ -z "$DISK_PID" ]; then
        echo "监控工具启动失败"
        exit 1
    fi

    echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

    data_file1="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
    
    #  gdb --args

    db_directory="/mntdisk/prism10B_ssd/memory16G_write_kv24+1380_cluster${cluster_a}"
                
    # 检查目录是否为空，如果不为空则删除所有内容
    if [ "$(ls -A $db_directory)" ]; then
        rm -rf "${db_directory:?}/"*
    fi

    if [ ! -d "$db_directory" ]; then
        mkdir -p "$db_directory"
    fi

    
    cgexec -g memory:group64  $EXECUTABLE \
        --db_path=$db_directory \
        --data_file_path=$data_file1 \
        --num_=1302137880 \
        --db_paths="{{/mnt/pmem1/prismdb10B_optane,$((pm_size * 1024 * 1024 * 1024))},{/mntdisk/prism10B_ssd/,1000000000}}" \
        --migration_metric=1 \
        --migration_policy=2 \
        --migration_rand_range_num=8 \
        --migration_rand_range_size=1048576 \
        --migrations_logging=true \
        --key_size=24 \
        --num_keys=2000000000 \
        --pop_cache_size=80000000 \
        --mem_log_file=$memory_log_file \
        --read_logging=true \
        --slab_dir=/mnt/pmem1/prismdb10B_optane/slab-0-00-001 \
        --latency_log_file=$lat_log_file 

    sleep 1

    DB_BENCH_PID=$(pgrep -af "rocksdb-kvexe --db_path=$db_directory" | grep -v 'sudo' | awk '{print $1}')
    echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

    wait $DB_BENCH_PID

    # 等待程序运行结束后，杀掉 pcm-memory 和 iostat 进程
    echo "程序执行完毕，开始杀掉 pcm-memory 和 iostat 监控进程..."
    kill "$PCM_PID"
    kill "$DISK_PID"


done     

# 如果有其它参数，可以在这里添加
