sudo rm -rf /mnt/nvme/level8B*
sudo rm  /mnt/ssd/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
range_dividers=(1)

declare -A value_size_to_scale=( [256]="1.0" [512]="0.5" [1024]="0.25" [2048]="0.125" )


for i in {2..2}; do
    base_num=$(($billion * $i))
    dir1="${i}B_leveldb"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
    for divider in ${range_dividers[@]}; do
        dir2="${dir1}/divider_$divider"
        if [ ! -d "$dir2" ]; then
            mkdir $dir2
        fi
        for value_size in 256 512 1024 2048; do
            dir3="${dir2}/value_size_$value_size"
            if [ ! -d "$dir3" ]; then
                mkdir $dir3
            fi
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            current_range=$(($num_entries / $divider))
            stats_interva=$((num_entries / 10))

            num_format=${value_size_to_scale[$value_size]} 

            for zipf_a in 1.01 1.1 1.2 1.3 1.4 1.5; do
                log_file="leveldb_${num_format}B_val_${value_size}_zipf_{$zipf_a}.log"
                data_file="/home/wangzizhao/workloads/zipf_keys{num_format}B_zipf${zipf_a}.csv" # 构建数据文件路径
                cd $dir3
                # 如果日志文件存在，则跳过当前迭代
                if [ -f "$log_file" ]; then
                    echo "Log file $log_file already exists. Skipping this iteration."
                    cd ../../..
                    continue
                fi
                echo "base_num: $base_num"
                echo "num_entries: $num_entries"
                echo "current_range: $divider"
                echo "value_size:$value_size"
                echo "stats_interval: $stats_interva"
                echo "$num_format B"
                echo "zipf_distrivution: $zipf_a"

                sudo gdb --args ../../../../KV_stores/leveldb/build/db_bench \
                --db=/mnt/nvme/level8B \
                --num=$num_entries \
                --value_size=$value_size \
                --batch_size=1000 \
                --benchmarks=fillzipf,stats \
                --range=$current_range\
                --logpath=/mnt/ssd \
                --bloom_bits=10 \
                --log=1  \
                --cache_size=8388608 \
                --open_files=40000 \
                --stats_interval=$stats_interva \
                --histogram=1 \
                --write_buffer_size=67108864 \
                --data_file=$data_file \
                --max_file_size=67108864   \
                --print_wa=true \
                | tee $log_file  \

                sleep 3
                
                sudo rm -rf /mnt/nvm/*
                sudo rm  /mnt/ssd/*.log
            done
            cd ../../..
        done
    done
done



