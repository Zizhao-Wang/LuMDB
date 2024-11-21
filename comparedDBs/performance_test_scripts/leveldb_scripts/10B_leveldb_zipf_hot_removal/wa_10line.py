def extract_every_100th_line(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8') as infile, open(output_file, 'w', encoding='utf-8') as outfile:
        count = 0  # 初始化第一列的计数为0
        for i, line in enumerate(infile, start=1):
            if i % 100 == 0:  # 每100行提取一行
                modified_line = f"{count} {line.split(' ', 1)[1]}"  # 替换第一列为count，并保持其他内容不变
                outfile.write(modified_line)
                count += 1  # 每写一行，第一列的数值加1

# 使用示例
input_file = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hot_removal/leveldb_wa_outputfile_mem1MB_1.3.txt'  # 替换为你的文件路径
output_file = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hot_removal/leveldb_wa_outputfile_mem1MB_1.3(1-10).txt'  # 替换为输出文件的路径
extract_every_100th_line(input_file, output_file)
