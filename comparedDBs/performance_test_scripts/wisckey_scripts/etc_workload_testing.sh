sudo rm -rf /mnt/nvme/level8B*
sudo rm  /mnt/ssd/*.log

echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
range_dividers=(1 4 8)
num_entries=500000000
stats_interval=$((num_entries / 10))

mkdir -p ./wisckey_500M

for a in 1.1 1.2 1.3 1.4 1.5;  do  

    data_file="/home/wangzizhao/workloads/etc_data_zipf${a}.csv" # 构建数据文件路径
    log_file="./wisckey_500M/wisckey_${num_entries}_variable_val_etc_${a}.log" # 构建日志文件名

    sudo smartctl -a /dev/nvme1n1 | sudo tee -a $log_file > /dev/null

    sudo ../wisckey/release/db_bench \
        --db=/mnt/nvme/level8B  \
        --num=$num_entries \
        --benchmarks=filletc,stats \
        --bloom_bits=10 \
        --cache_size=8388608  \
        --open_files=800000  \
        --histogram=1 \
        --print_wa=true \
        --stats_interval=$stats_interval \
        --write_buffer_size=67108864  \
        --max_file_size=67108864 \
        --data_file=$data_file \
        | sudo tee -a $log_file 
    
    sudo smartctl -a /dev/nvme1n1 | sudo tee -a $log_file > /dev/null


    sudo rm -rf /mnt/nvme/level8B*
    sudo rm  /mnt/ssd/*.log
done
