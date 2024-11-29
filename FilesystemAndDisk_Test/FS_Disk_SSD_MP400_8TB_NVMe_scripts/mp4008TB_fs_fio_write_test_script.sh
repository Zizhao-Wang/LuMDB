#!/bin/bash

# 设定文件路径和大小
# 使用从父脚本导出的FILENAME变量
echo "Using device: $FILENAME"

# SIZE=107374182400  

OUTPUT_DIR=./mp4008TB_fs_fio_test_write_ranseq



for NUMJOBS in 4 8 16 32 64 1 2
do

    SIZE_PER_JOB=$(($SIZE / $NUMJOBS))
    echo "write unit per job: $SIZE_PER_JOB bytes"

    for IODEPTH in 1 2 4 8 16 32
    do
        for BLOCK_SIZE in 4K 8K 16k 32K 64k 128K 256k 512k 1M 2M 4M 8M
        do

            FIO_OUTPUT_FILE="$OUTPUT_DIR/rand_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out"
            if [ -f "$FIO_OUTPUT_FILE" ]; then
                echo "Output file $FIO_OUTPUT_FILE already exists. Skipping FIO command."
            else
                # 随机写测试
                iostat -d $IOSTAT_INTERVAL -x $DEVICENAME > $OUTPUT_DIR/rand_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs_IOstats.out &
                PID_IOSTAT=$!
                fio --name=rand_write_test \
                    --rw=randwrite \
                    --bs=$BLOCK_SIZE \
                    --size=$SIZE_PER_JOB \
                    --numjobs=$NUMJOBS \
                    --ioengine=libaio \
                    --direct=1 \
                    --group_reporting \
                    --filename=$FILENAME \
                    --output=$OUTPUT_DIR/rand_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out &
                sleep 1
                # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
                FIO_COMMAND_PATTERN="fio --name=rand_write_test --rw=randwrite --bs=$BLOCK_SIZE --size=$SIZE_PER_JOB --numjobs=$NUMJOBS --ioengine=libaio --direct=1 --filename=$FILENAME"
                # SECOND_SMALLEST_FIO_PID=$(pgrep -af "$FIO_COMMAND_PATTERN" | awk '{print $1}' | sort -n | sed -n '2p')
                # # 检查是否有进程ID被捕获
                # if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
                #     echo "No suitable FIO process found. Exiting."
                #     exit 1
                # else
                #     echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
                # fi
                # # 使用perf监控选定的PID
                # perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_rand_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.txt" &
                # 等待原始的 fio 进程结束
                wait $!
                
                # 在 write.sh 中调用 all.sh 中定义的函数
                append_device_info "$OUTPUT_DIR/rand_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out" "$FILENAME"
                kill $PID_IOSTAT
            fi

            FIO_OUTPUT_FILE="$OUTPUT_DIR/seq_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out"
            if [ -f "$FIO_OUTPUT_FILE" ]; then
                echo "Output file $FIO_OUTPUT_FILE already exists. Skipping FIO command."
            else
                # 顺序写测试
                iostat -d $IOSTAT_INTERVAL -x $DEVICENAME > $OUTPUT_DIR/seq_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs_IOstats.out &
                PID_IOSTAT=$!
                fio --name=seq_write_test \
                    --rw=write \
                    --bs=$BLOCK_SIZE \
                    --size=$SIZE_PER_JOB \
                    --numjobs=$NUMJOBS \
                    --ioengine=libaio \
                    --direct=1 \
                    --group_reporting \
                    --filename=$FILENAME \
                    --output=$OUTPUT_DIR/seq_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out &
                sleep 1
                # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
                FIO_COMMAND_PATTERN="fio --name=seq_write_test --rw=write --bs=$BLOCK_SIZE --size=$SIZE_PER_JOB --numjobs=$NUMJOBS --ioengine=libaio --direct=1 --filename=$FILENAME "
                # SECOND_SMALLEST_FIO_PID=$(pgrep -af "$FIO_COMMAND_PATTERN" | awk '{print $1}' | sort -n | sed -n '2p')
                # # 检查是否有进程ID被捕获
                # if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
                #     echo "No suitable FIO process found. Exiting."
                #     exit 1
                # else
                #     echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
                # fi
                # # 使用perf监控选定的PID
                # perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_seq_write_test_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.txt" &
                # 等待原始的 fio 进程结束
                wait $!
                
                # 在 write.sh 中调用 all.sh 中定义的函数
                append_device_info "$OUTPUT_DIR/seq_write_block_${BLOCK_SIZE}_${IODEPTH}_${NUMJOBS}_jobs.out" "$FILENAME"
                kill $PID_IOSTAT
            fi
        done
    done
done

echo "所有写操作测试已完成。"
