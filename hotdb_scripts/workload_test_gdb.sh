BASE_VALUE_SIZE=128
billion=1000000

for i in {1..1}; do
    base_num=$(($billion * $i))
    dir1="hotdb_10B"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 10))
            key_size=16

            for zipf_a in 1.02 ; do  #1.2 1.3 1.4
                    percentages1=(1 ) #5 10 15 20 25 30

                    data_file="/home/jeff-wang/hotdb_workloads_test/zipf${zipf_a}_keys${key_size}_value${value_size}_1.0M.csv" # 构建数据文件路径
                    # hot_files=$(printf "/home/jeff-wang/workloads/zipf${zipf_a}_top%%s_keys10B.csv," {1,5,10,15,20,25,30})
                    # hot_files=${hot_files%?} # 移除最后一个逗号

                   


                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    for hot_batchs in 1000 ; do  #1500 2000 2500 3000 3500 4000 4500 5000 5500 6000 6500 7000 8000 9000 10000
                    sudo gdb --args  ../../hotdb/release/db_bench \
                    --db=/mnt/nvm/level8B \
                    --num=$num_entries \
                    --value_size=$value_size \
                    --batch_size=1000 \
                    --benchmarks=fillrandom,stats \
                    --data_file=$data_file  \
                    --logpath=/mnt/logs \
                    --bloom_bits=10 \
                    --log=1  \
                    --hot_init_range=5 \
                    --hot_batch=$hot_batchs \
                    --cache_size=8388608 \
                    --open_files=40000 \
                    --stats_interval=$stats_interva \
                    --histogram=1 \
                    --write_buffer_size=67108864 \
                    --max_file_size=67108864   \
                    --print_wa=true 
                    done
            done
        done
done
