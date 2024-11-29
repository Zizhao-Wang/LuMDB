#!/bin/bash

# 设定文件路径和大小
# 使用从父脚本导出的FILENAME变量
echo "Using device: $FILENAME"
SIZE=214748364800
OUTPUT_DIR=./MP700_fio_test_read_ranseq_multi_queue


for BLOCK_SIZE in 4K 8K 16k 64k 128K 256k 512k 1M 2M 4M 8M
    do
    # 循环测试从1个线程到32个线程
    for NUMJOBS in {1..1}
    do

        for IODEPTH in 2 4 8 16 32
        do
            # 顺序读测试
            iostat -d 1 -x $FILENAME > $OUTPUT_DIR/seq_read_test_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs_IOstats.out &
            PID_IOSTAT=$!

            SIZE_PER_JOB=$(($SIZE / $NUMJOBS))
            SIZE_PER_JOB_IN_G=$(echo "scale=2; $SIZE_PER_JOB / (1024*1024*1024)" | bc)
            echo "write unit per job: $SIZE_PER_JOB bytes $SIZE_PER_JOB_IN_G GiB"

            sudo fio --name=seq_read_test \
                --rw=read \
                --bs=$BLOCK_SIZE \
                --size=$SIZE_PER_JOB \
                --numjobs=$NUMJOBS \
                --ioengine=libaio \
                --iodepth=$IODEPTH \
                --direct=1 \
                --group_reporting \
                --filename=$FILENAME \
                --output=$OUTPUT_DIR/seq_read_test_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out

            # 结束 iostat 进程
            sudo kill $PID_IOSTAT

            # 随机读测试
            iostat -d 1 -x $FILENAME > $OUTPUT_DIR/rand_read_test_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs_IOstats.out &
            PID_IOSTAT=$!
            sudo fio --name=rand_read_test \
                --rw=randread \
                --bs=$BLOCK_SIZE \
                --size=$SIZE_PER_JOB \
                --numjobs=$NUMJOBS \
                --iodepth=$IODEPTH \
                --ioengine=libaio \
                --direct=1 \
                --group_reporting \
                --filename=$FILENAME \
                --output=$OUTPUT_DIR/rand_read_test_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out
            # 结束 iostat 进程
            sudo kill $PID_IOSTAT
        done
    done
done


echo "所有测试已完成。"
