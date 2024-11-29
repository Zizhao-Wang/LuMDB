#!/bin/bash

# 设定文件路径和大小
FILENAME=/dev/sdb1
SIZE=200G
BLOCK_SIZE=4k
OUTPUT_DIR=./wdhdd_fio_test_read_disk

# 循环测试从1个线程到32个线程
for NUMJOBS in {1..32}
do
    # 随机读测试
    sudo fio --name=rand_read_test \
        --rw=randread \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/rand_read_test_${NUMJOBS}_jobs.out

    # 顺序读测试
    sudo fio --name=seq_read_test \
        --rw=read \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/seq_read_test_${NUMJOBS}_jobs.out
done

echo "所有测试已完成。"
