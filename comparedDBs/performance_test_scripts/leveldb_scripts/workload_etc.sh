sudo rm -rf /mnt/nvme/level8B*
sudo rm  /mnt/ssd/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
range_dividers=(1 4 8)
num_entries=500000000
stats_interval=$((num_entries / 10))

# log_file="shcdb_${num_entries}_variable_val_etc.log"

# cgexec -g memory:kv128 

for a in 1.5 1.01;  do  

    data_file="/home/wangzizhao/workloads/etc_data_zipf${a}.csv" # 构建数据文件路径
    log_file="shcdb_${num_entries}_variable_val_etc_${a}.log" # 构建日志文件名

    sudo ../KV_stores/leveldb/build/db_bench \
        --db=/mnt/nvme/level8B  \
        --num=$num_entries \
        --benchmarks=filletc,stats \
        --bloom_bits=10 \
        --cache_size=8388608  \
        --open_files=40000  \
        --histogram=1 \
        --print_wa=true \
        --stats_interval=$stats_interval \
        --write_buffer_size=67108864  \
        --max_file_size=67108864 \
        --data_file=$data_file \
        | tee $log_file
    
    sudo rm -rf /mnt/nvme/level8B*
    sudo rm  /mnt/ssd/*.log
done

# ../kv/release/tests[表情]/test_kv_test \
#     --hugepage=true \
#     --db=/mnt/nvm/kv8B \
#     --num=10000000 \
#     --value_size=100 \
#     --batch_size=1000 \
#     --range=100000\
#     --benchmarks=fillrandom,stats \
#     --logpath=/mnt/ssd \
#     --bloom_bits=10 \
#     --log=true \
#     --cache_size=8388608 \
#     --low_pool=3 \
#     --high_pool=3 \
#     --open_files=40000 \
#     --stats_interval=100000000 \
#     --histogram=true \
#     --compression=0 \
#     --write_buffer_size=2097152 \
#     --skiplistrep=false \
#     --log_dio=true \
#     --partition=100 \
#     --print_wa=true \
#     | tee kv8B_nvm_hugepage_value_100.log \
#     | grep "WriteAmplification: " \
#     | awk -v n="$num" -v r="$current_range" -v v="$value_size" '{print n, r, v, $0}' >> write_amplification.txt