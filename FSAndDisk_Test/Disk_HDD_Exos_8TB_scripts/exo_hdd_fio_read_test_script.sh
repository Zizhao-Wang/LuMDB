#!/bin/bash

# 使用从父脚本导出的FILENAME变量
echo "Using device: $FILENAME"
SIZE=200G
BLOCK_SIZE=4k
OUTPUT_DIR=./exo_hdd_fio_test_read_ranseq

# 循环测试从1个线程到32个线程
for NUMJOBS in {17..32}
do
    # 顺序读测试
    iostat -d 100 -x $FILENAME > $OUTPUT_DIR/seq_read_test_${NUMJOBS}_jobs_IOstats.out &
    PID_IOSTAT=$!
    sudo fio --name=seq_read_test \
        --rw=read \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/seq_read_test_${NUMJOBS}_jobs.out \
        --runtime=3600
    # 结束 iostat 进程
    kill $PID_IOSTAT
    append_device_info "$OUTPUT_DIR/seq_read_test_${NUMJOBS}_jobs.out" "$FILENAME"

    # 随机读测试
    iostat -d 100 -x $FILENAME > $OUTPUT_DIR/rand_read_test_${NUMJOBS}_jobs_IOstats.out &
    PID_IOSTAT=$!
    sudo fio --name=rand_read_test \
        --rw=randread \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/rand_read_test_${NUMJOBS}_jobs.out \
        --runtime=3600
    # 结束 iostat 进程
    kill $PID_IOSTAT
    append_device_info "$OUTPUT_DIR/rand_read_test_${NUMJOBS}_jobs.out" "$FILENAME"

    
done

echo "所有测试已完成。"
