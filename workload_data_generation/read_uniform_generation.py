import pandas as pd
import random

def process_csv(input_file, n, m, output_dedup_file, output_random_file):
    # 读取前 n 行数据
    data = pd.read_csv(input_file, nrows=n)
    
    # 去重操作
    dedup_data = data['Key'].drop_duplicates().reset_index(drop=True)
    
    # 保存去重后的文件
    dedup_data.to_csv(output_dedup_file, index=False)
    print(f"Deduplicated data saved to {output_dedup_file}")
    
    # 从去重数据中随机选择 m 个数据
    if m > len(dedup_data):
        print("Warning: The requested number of samples exceeds available unique keys. Sampling all available keys.")
        sampled_data = dedup_data
    else:
        sampled_data = dedup_data.sample(n=m, random_state=None).reset_index(drop=True)

    # 保存随机选择的数据文件
    sampled_data.to_csv(output_random_file, index=False)
    print(f"Randomly sampled data saved to {output_random_file}")

# 参数设置
input_file = '/home/jeff-wang/workloads/zipf1.4_keys10.0B.csv'  # 输入文件路径
n = 100000000  # 提取前 n 行数据并去重
m = 1000   # 从去重后的数据中随机选取 m 个
output_dedup_file = '/home/jeff-wang/workloads/zipf1.4_dedup_100M_keys.csv'  # 去重数据保存路径
output_random_file = '/home/jeff-wang/workloads/zipf1.4_random_select_1000_read_keys.csv'  # 随机选择数据保存路径

# 执行处理
process_csv(input_file, n, m, output_dedup_file, output_random_file)
