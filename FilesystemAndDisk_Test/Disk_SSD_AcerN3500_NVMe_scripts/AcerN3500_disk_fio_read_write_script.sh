OUTPUT_DIR=./AcerN3500_fio_test_readwrite_ranseq
# 设定文件路径和大小
# 使用从父脚本导出的FILENAME变量
echo "Using device: $FILENAME"
NUMJOBS=1

# 循环不同的块大小
for BLOCK_SIZE in 4k 8k 16k 32k 64k 128k 256k 512k 1M 2M 4M 8M
do
    echo "Testing with block size: $BLOCK_SIZE"

    # 随机读测试
    TEST_NAME="rand_read_test"
    iostat -d 1 -x $FILENAME > $OUTPUT_DIR/${TEST_NAME}_${BLOCK_SIZE}_4k_8m_IOstats.out &
    PID_IOSTAT=$!
    fio --name=$TEST_NAME \
        --rw=randread \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/${TEST_NAME}_${BLOCK_SIZE}_4k_8m.out &
    
    sleep 1
    # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
    SECOND_SMALLEST_FIO_PID=$(pgrep -af "fio --name=$TEST_NAME" | awk '{print $1}' | sort -n | sed -n '2p')
    # 检查是否有进程ID被捕获
    if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
        echo "No suitable FIO process found. Exiting."
        exit 1
    else
        echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
    fi
    # 使用perf监控选定的PID
    perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_${TEST_NAME}_${BLOCK_SIZE}_4k_8m.txt" &
    # 等待原始的 fio 进程结束
    wait $!
    
    append_device_info "$OUTPUT_DIR/${TEST_NAME}_${BLOCK_SIZE}_4k_8m.out" "$FILENAME"
    # 结束 iostat 进程
    kill $PID_IOSTAT

    # 随机写测试
    TEST_NAME2="rand_write_test"
    iostat -d 1 -x $FILENAME > $OUTPUT_DIR/${TEST_NAME2}_${BLOCK_SIZE}_4k_8m_IOstats.out &
    PID_IOSTAT=$!
    fio --name=$TEST_NAME2 \
        --rw=randwrite \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/${TEST_NAME2}_${BLOCK_SIZE}_4k_8m.out &
    
    sleep 1
    # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
    SECOND_SMALLEST_FIO_PID=$(pgrep -af "fio --name=$TEST_NAME2" | awk '{print $1}' | sort -n | sed -n '2p')
    # 检查是否有进程ID被捕获
    if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
        echo "No suitable FIO process found. Exiting."
        exit 1
    else
        echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
    fi
    # 使用perf监控选定的PID
    perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_${TEST_NAME2}_${BLOCK_SIZE}_4k_8m.txt" &
    # 等待原始的 fio 进程结束
    wait $!
    
    kill $PID_IOSTAT
    append_device_info "$OUTPUT_DIR/${TEST_NAME2}_${BLOCK_SIZE}_4k_8m.out" "$FILENAME"

    # 顺序读测试
    TEST_NAME3="seq_read_test"
    iostat -d 1 -x $FILENAME > $OUTPUT_DIR/${TEST_NAME3}_${BLOCK_SIZE}_4k_8m_IOstats.out &
    PID_IOSTAT=$!
    fio --name=$TEST_NAME3 \
        --rw=read \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/${TEST_NAME3}_${BLOCK_SIZE}_4k_8m.out &

    sleep 1
    # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
    SECOND_SMALLEST_FIO_PID=$(pgrep -af "fio --name=$TEST_NAME3" | awk '{print $1}' | sort -n | sed -n '2p')
    # 检查是否有进程ID被捕获
    if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
        echo "No suitable FIO process found. Exiting."
        exit 1
    else
        echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
    fi
    # 使用perf监控选定的PID
    perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_${TEST_NAME3}_${BLOCK_SIZE}_4k_8m.txt" &
    # 等待原始的 fio 进程结束
    wait $!
    
    kill $PID_IOSTAT
    
    append_device_info "$OUTPUT_DIR/${TEST_NAME3}_${BLOCK_SIZE}_4k_8m.out" "$FILENAME"

    # 顺序写测试
    TEST_NAME4="seq_write_test"
    iostat -d 1 -x $FILENAME > $OUTPUT_DIR/${TEST_NAME4}_${BLOCK_SIZE}_4k_8m_IOstats.out &
    PID_IOSTAT=$!
     fio --name=$TEST_NAME4 \
        --rw=write \
        --bs=$BLOCK_SIZE \
        --size=$SIZE \
        --numjobs=$NUMJOBS \
        --ioengine=libaio \
        --direct=1 \
        --group_reporting \
        --filename=$FILENAME \
        --output=$OUTPUT_DIR/${TEST_NAME4}_${BLOCK_SIZE}_4k_8m.out &
    
    sleep 1
    # 使用 pgrep 捕获所有相关的 fio 进程ID，排序后选取第二个最小的 PID
    SECOND_SMALLEST_FIO_PID=$(pgrep -af "fio --name=$TEST_NAME4" | awk '{print $1}' | sort -n | sed -n '2p')
    # 检查是否有进程ID被捕获
    if [ -z "$SECOND_SMALLEST_FIO_PID" ]; then
        echo "No suitable FIO process found. Exiting."
        exit 1
    else
        echo "Second smallest FIO PID found: $SECOND_SMALLEST_FIO_PID"
    fi
    # 使用perf监控选定的PID
    perf stat -p $SECOND_SMALLEST_FIO_PID 2>&1 | tee "$OUTPUT_DIR/perfstat_${TEST_NAME4}_${BLOCK_SIZE}_4k_8m.txt" &
    # 等待原始的 fio 进程结束
    wait $!
    kill $PID_IOSTAT
    append_device_info "$OUTPUT_DIR/${TEST_NAME4}_${BLOCK_SIZE}_4k_8m.out" "$FILENAME"
done

echo "所有读写操作测试已完成。"
