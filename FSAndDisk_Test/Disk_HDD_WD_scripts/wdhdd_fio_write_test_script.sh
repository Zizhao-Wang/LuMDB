#!/bin/bash

# 设定文件路径和大小
FILENAME=/dev/sdb1
SIZE=200G
BLOCK_SIZE=4k
OUTPUT_DIR=./wdhdd_fio_test_write_ranseq
# 循环测试从1个线程到32个线程
for NUMJOBS in {1..32}
do
    # 随机写测试
    sudo fio --name=rand_write_test \
        --rw=randwrite \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/rand_write_test_${NUMJOBS}_jobs.out

    # 顺序写测试
    sudo fio --name=seq_write_test \
        --rw=write \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/seq_write_test_${NUMJOBS}_jobs.out
done

echo "所有写操作测试已完成。"
