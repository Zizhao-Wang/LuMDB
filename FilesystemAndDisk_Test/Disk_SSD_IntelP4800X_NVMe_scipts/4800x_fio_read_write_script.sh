OUTPUT_DIR=./4800x_fio_test_readwrite_ranseq
# 设定文件路径和大小
# 使用从父脚本导出的FILENAME变量
echo "Using device: $FILENAME IOSTAT INTERVAL: $IOSTAT_INTERVAL"
SIZE=200G
NUMJOBS=1

# 循环不同的块大小
for BLOCK_SIZE in 4k 8k 16k 32k 64k 128k 256k 512k 1M 2M 4M 8M
do
    echo "Testing with block size: $BLOCK_SIZE"

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
        --output=$OUTPUT_DIR/rand_read_test_${BLOCK_SIZE}_4k_8m.out

    # # 随机写测试
    # sudo fio --name=rand_write_test \
    #     --rw=randwrite \
    #     --bs=$BLOCK_SIZE \
    #     --size=$SIZE \
    #     --numjobs=$NUMJOBS \
    #     --ioengine=libaio \
    #     --direct=1 \
    #     --group_reporting \
    #     --filename=$FILENAME \
    #     --output=$OUTPUT_DIR/rand_write_test_${BLOCK_SIZE}_4k_8m.out

    # # 顺序读测试
    # sudo fio --name=seq_read_test \
    #     --rw=read \
    #     --bs=$BLOCK_SIZE \
    #     --size=$SIZE \
    #     --numjobs=$NUMJOBS \
    #     --ioengine=libaio \
    #     --direct=1 \
    #     --group_reporting \
    #     --filename=$FILENAME \
    #     --output=$OUTPUT_DIR/seq_read_test_${BLOCK_SIZE}_4k_8m.out

    # # 顺序写测试
    # sudo fio --name=seq_write_test \
    #     --rw=write \
    #     --bs=$BLOCK_SIZE \
    #     --size=$SIZE \
    #     --numjobs=$NUMJOBS \
    #     --ioengine=libaio \
    #     --direct=1 \
    #     --group_reporting \
    #     --filename=$FILENAME \
    #     --output=$OUTPUT_DIR/seq_write_test_${BLOCK_SIZE}_4k_8m.out
done

echo "所有读写操作测试已完成。"
