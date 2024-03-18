import csv

def compare_csv_files(file1, file2, output_file):
    with open(file1, 'r') as f1, open(file2, 'r') as f2, open(output_file, 'w', newline='') as fout:
        reader1 = csv.reader(f1)
        reader2 = csv.reader(f2)
        writer = csv.writer(fout)
        
        # 跳过标题行
        next(reader1)
        next(reader2)
        
        # 写入标题行到输出文件
        writer.writerow(['Line', 'Key Match', 'Frequency Match', 'Key1', 'Frequency1', 'Key2', 'Frequency2'])
        
        # 初始化行号
        line_number = 1
        
        # 逐行读取并比较
        for row1, row2 in zip(reader1, reader2):
            key_match = row1[0] == row2[0]
            frequency_match = row1[1] == row2[1]
            writer.writerow([line_number, key_match, frequency_match, row1[0], row1[1], row2[0], row2[1]])
            line_number += 1

# 示例用法
cpp_test_file = '/home/jeff-wang/workloads/cpp_test.csv'
python_test_file = '/home/jeff-wang/workloads/python_test.csv'
output_comparison_file = '/home/jeff-wang/workloads/comparison_results.csv'

compare_csv_files(cpp_test_file, python_test_file, output_comparison_file)
