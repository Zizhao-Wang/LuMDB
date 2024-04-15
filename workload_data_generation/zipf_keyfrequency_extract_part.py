import csv
import os

def create_output_files(csv_file_path, output_directory, max_files=20):
    # 确保输出目录存在
    os.makedirs(output_directory, exist_ok=True)

    with open(csv_file_path, 'r', newline='') as csvfile:
        reader = csv.reader(csvfile)
        headers = next(reader)  # 读取标题行
        
        rows = []  # 存储读取的行
        for i, row in enumerate(reader):
            if i >= max_files:
                break  # 只读取需要的行数
            rows.append(row)

        # 输出文件
        for i in range(len(rows)):
            output_file_path = os.path.join(output_directory, f"output_{i + 1}.csv")
            with open(output_file_path, 'w', newline='') as output_file:
                writer = csv.writer(output_file)
                writer.writerow(headers)  # 写入标题
                writer.writerows(rows[:i + 1])  # 写入当前文件的行数

    print(f"Files have been created successfully up to: output_{len(rows)}.csv")

# 使用示例
csv_file_path = '/home/jeff-wang/workloads//zipf1.1_top10_keys10.0B.csv'
output_directory = '/home/jeff-wang/workloads/wa_reduce_test_workloads'
create_output_files(csv_file_path, output_directory, max_files=5)
