import pandas as pd
import random

def process_csv(input_file, n, m, k, output_ops_file):
    # 读取第 n+1 行到 n+1+10亿行数据，用于插入操作
    skip_rows = n  # 跳过前 n 行
    data_after_n = pd.read_csv(input_file, skiprows=range(1, skip_rows+1), nrows=1000000000, header=None, names=['Key'])
    
    # 去重操作
    dedup_data_after_n = data_after_n['Key'].drop_duplicates().reset_index(drop=True)
    print(f"从第 {n+1} 行开始的数据去重后的数量：{dedup_data_after_n.size}")
    
    # 从去重后的数据中随机选择 m 个作为插入操作
    if m > len(dedup_data_after_n):
        print("警告：请求的插入样本数量超过可用的唯一 keys 数量。将使用所有可用的 keys。")
        insert_keys = dedup_data_after_n
    else:
        insert_keys = dedup_data_after_n.sample(n=m, random_state=None).reset_index(drop=True)
    
    # 从插入操作中随机选择 k 个作为读取操作，允许重复
    read_keys = random.choices(insert_keys, k=k)  # 使用 random.choices 来允许重复选择

    # 为插入和读取操作添加操作类型
    insert_ops = pd.DataFrame({'Operation': 'INSERT', 'Key': insert_keys})
    read_ops = pd.DataFrame({'Operation': 'READ', 'Key': read_keys})
    
    # 合并插入和读取操作
    all_ops = pd.concat([insert_ops, read_ops], ignore_index=True)
    
    # 随机打乱顺序
    all_ops = all_ops.sample(frac=1).reset_index(drop=True)
    
    # 保存结果到新的文件
    all_ops.to_csv(output_ops_file, index=False)
    print(f"操作数据已保存到 {output_ops_file}")

# 参数设置
input_file = '/home/jeff-wang/workloads/zipf1.2_keys10.0B.csv'  # 输入文件路径
n = 1000000000  # 提取前 n 行数据并去重
m = 50000   # 从第 n+1 行开始的去重数据中随机选取 m 个插入操作
k = 950000  # 从插入操作中随机选取 k 个读取操作（允许重复）

output_ops_file = '/home/jeff-wang/workloads/ycsb_d_workload.csv'  # 最终操作数据保存路径

# 执行处理
process_csv(input_file, n, m, k, output_ops_file)
