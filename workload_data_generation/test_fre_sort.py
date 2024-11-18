import csv
from collections import defaultdict

def calculate_key_frequencies(input_file_name, output_file_name):
    # 使用defaultdict来计数，初始化为0
    frequencies = defaultdict(int)

    # 从CSV文件中读取数据
    with open(input_file_name, mode='r', newline='') as file:
        reader = csv.reader(file)
        header = next(reader)  # 跳过标题行
        for row in reader:
            key = row[0]  # 假设key位于每行的第一列
            frequencies[key] += 1

    frequencies_sorted = sorted(frequencies.items(), key=lambda item: item[1], reverse=True)

    # 将频率结果写入到一个新的CSV文件中
    with open(output_file_name, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['Key', 'Frequency'])  # 写入标题行
        for key, freq in frequencies_sorted:
            writer.writerow([key, freq])

# 示例使用
input_file_name = "/home/jeff-wang/workloads/zipf1.02_keys10.0B.csv"
output_file_name = "/home/jeff-wang/workloads/python_test.csv"
calculate_key_frequencies(input_file_name, output_file_name)
