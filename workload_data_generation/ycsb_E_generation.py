import pandas as pd
import random

def process_csv(input_file, n, m, k, output_ops_file):
    # 读取前 n 行数据，用于插入操作
    data_before_n = pd.read_csv(input_file, nrows=n, header=None, names=['Key'])
    
    # 去重操作
    dedup_data_before_n = data_before_n['Key'].drop_duplicates().reset_index(drop=True)
    print(f"前 {n} 行数据去重后的数量：{dedup_data_before_n.size}")
    
    # 从前 n 行数据中随机选择 m 个作为插入操作
    if m > len(dedup_data_before_n):
        print("警告：请求的插入样本数量超过可用的唯一 keys 数量。将使用所有可用的 keys。")
        insert_keys = dedup_data_before_n
    else:
        insert_keys = dedup_data_before_n.sample(n=m, random_state=None).reset_index(drop=True)
    
    # 读取第 n+1 到 n+1+10亿行数据，用于扫描操作
    skip_rows = n  # 跳过前 n 行
    data_after_n = pd.read_csv(input_file, skiprows=range(1, skip_rows+1), nrows=k, header=None, names=['Key'])
    
    # 直接从第 n+1 行开始选择 k 个数据作为扫描操作（逐行读取）
    scan_keys = data_after_n['Key'].reset_index(drop=True)
    print(f"从第 {n+1} 行开始的数据中逐行选取了 {k} 个作为扫描操作")

    # 为插入和扫描操作添加操作类型
    insert_ops = pd.DataFrame({'Operation': 'INSERT', 'Key': insert_keys})
    scan_ops = pd.DataFrame({'Operation': 'SCAN', 'Key': scan_keys})
    
    # 合并插入和扫描操作
    all_ops = pd.concat([insert_ops, scan_ops], ignore_index=True)
    
    # 随机打乱顺序
    all_ops = all_ops.sample(frac=1).reset_index(drop=True)
    
    # 保存结果到新的文件
    all_ops.to_csv(output_ops_file, index=False)
    print(f"操作数据已保存到 {output_ops_file}")

# 参数设置
input_file = '/home/jeff-wang/workloads/zipf1.2_keys10.0B.csv'  # 输入文件路径
n = 1000000000  # 提取前 n 行数据并去重
m = 50000   # 从前 n 行数据中随机选取 m 个插入操作
k = 950000  # 从第 n+1 行开始的数据中逐行读取 k 个作为扫描操作

output_ops_file = '/home/jeff-wang/workloads/ycsb_e_workload.csv'  # 最终操作数据保存路径

# 执行处理
process_csv(input_file, n, m, k, output_ops_file)
