sudo rm -rf /mnt/nvm/*
sudo rm  /mnt/ssd/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo ulimit -n 800000


BASE_VALUE_SIZE=128
billion=1000000000
range_dividers=(1 4 8)
num_entries=100000
stats_interval=$((num_entries / 10))

log_file="shcdb_${num_entries}_variable_val_etc.log"

# cgexec -g memory:kv128 

sudo gdb --args ../KV_stores/leveldb/build/db_bench \
    --db=/mnt/nvme/level8B  \
    --num=$num_entries \
    --benchmarks=filletc,stats \
    --bloom_bits=10 \
    --cache_size=8388608  \
    --open_files=40000  \
    --histogram=1 \
    --write_buffer_size=67108864  \
    --stats_interval=$stats_interval \
    --max_file_size=67108864   \
    | tee $log_file  \

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