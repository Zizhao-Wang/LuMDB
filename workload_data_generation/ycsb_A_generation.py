import pandas as pd
import random

def process_csv(input_file, n, m, k, output_ops_file):
    # 读取前 n 行数据
    data = pd.read_csv(input_file, nrows=n)
    
    # 去重操作
    dedup_data = data['Key'].drop_duplicates().reset_index(drop=True)
    print(f"去重后的数据量：{dedup_data.size}")
    
    # 从去重数据中随机选择 m 个数据作为读取操作
    if m > len(dedup_data):
        print("警告：请求的读取样本数量超过可用的唯一 keys 数量。将使用所有可用的 keys。")
        read_keys = dedup_data
    else:
        read_keys = dedup_data.sample(n=m, random_state=None).reset_index(drop=True)
    
    # 读取第 n+1 到 n+k 行数据作为更新操作
    skip_rows = n  # 跳过前 n 行
    update_data = pd.read_csv(input_file, skiprows=range(1, skip_rows+1), nrows=k, header=None, names=['Key'])
    update_keys = update_data['Key'].reset_index(drop=True)
    
    # 为读取和更新操作添加操作类型
    read_ops = pd.DataFrame({'Operation': 'READ', 'Key': read_keys})
    update_ops = pd.DataFrame({'Operation': 'UPDATE', 'Key': update_keys})
    
    # 合并读取和更新操作
    all_ops = pd.concat([read_ops, update_ops], ignore_index=True)
    
    # 随机打乱顺序
    all_ops = all_ops.sample(frac=1).reset_index(drop=True)
    
    # 保存结果到新的文件
    all_ops.to_csv(output_ops_file, index=False)
    print(f"操作数据已保存到 {output_ops_file}")

# 参数设置
input_file = '/home/jeff-wang/workloads/zipf1.2_keys10.0B.csv'  # 输入文件路径
n = 1000000000  # 提取前 n 行数据并去重
m = 5000000   # 从去重后的数据中随机选取 m 个读取操作
k = 5000000  # 从第 n+1 到 n+k 行数据提取 k 个更新操作

output_ops_file = '/home/jeff-wang/workloads/ycsb_a_workload.csv'  # 最终操作数据保存路径

# 执行处理
process_csv(input_file, n, m, k, output_ops_file)
