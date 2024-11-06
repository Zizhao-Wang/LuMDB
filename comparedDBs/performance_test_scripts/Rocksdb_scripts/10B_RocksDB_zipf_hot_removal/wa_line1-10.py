def extract_every_100th_line(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8') as infile, open(output_file, 'w', encoding='utf-8') as outfile:
        count = 0  # 初始化第一列的计数为0
        for i, line in enumerate(infile, start=1):
            if i % 100 == 0:  # 每100行提取一行
                line = line.strip()  # 去掉两端的空白字符
                line_parts = line.split('\t')  # 使用制表符分割
                
                if len(line_parts) > 1:
                    # 替换第一列为count，并保持其他内容不变
                    modified_line = str(count) + '\t' + '\t'.join(line_parts[1:])
                    outfile.write(modified_line + '\n')
                    print(modified_line)  # 输出修改后的内容
                else:
                    print(line)  # 输出原始行内容，如果分割不足

                count += 1  # 每写一行，第一列的数值加1

# 使用示例
input_file = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_mem8MiB_1.1waoutput.txt'  # 替换为你的文件路径
output_file = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_mem8MiB_1.1waoutput(1-10).txt'  # 替换为输出文件的路径
extract_every_100th_line(input_file, output_file)
