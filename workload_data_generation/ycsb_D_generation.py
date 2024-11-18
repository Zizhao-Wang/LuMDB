import pandas as pd
import random

def process_csv(input_file, n, k, m, read_ratio, output_ops_file):
    # 读取第 n+1 到 n+k 行数据作为更新操作
    skip_rows = n  # 跳过前 n 行
    update_data = pd.read_csv(input_file, skiprows=range(1, skip_rows+1), nrows=k, header=None, names=['Key'])
    update_keys = update_data['Key'].reset_index(drop=True)
    
    # 为更新操作添加操作类型
    update_ops = pd.DataFrame({'Operation': 'UPDATE', 'Key': update_keys})
    
    # 按读取比例生成读取操作，读取最新更新的键
    num_reads = m  # 读取操作数量
    read_keys = random.choices(update_keys, k=num_reads)  # 从更新的键中采样
    read_ops = pd.DataFrame({'Operation': 'READ', 'Key': read_keys})
    
    # 合并读取和更新操作
    all_ops = pd.concat([read_ops, update_ops], ignore_index=True)
    
    # 随机打乱顺序
    all_ops = all_ops.sample(frac=1).reset_index(drop=True)
    
    # 保存结果到新的文件
    all_ops.to_csv(output_ops_file, index=False)
    print(f"YCSB-D 数据集已保存到 {output_ops_file}")

# 参数设置
input_file = '/home/jeff-wang/workloads/zipf1.2_keys10.0B.csv'  # 输入文件路径
n = 1000000000  # 提取前 n 行数据并去重
k = 500000  # 从第 n+1 到 n+k 行数据提取 k 个更新操作
m = 9500000  # 读取操作占 95%

output_ops_file = '/home/jeff-wang/workloads/ycsb_d_workload.csv'  # 最终操作数据保存路径

# 执行处理
process_csv(input_file, n, k,m, read_ratio, output_ops_file)
