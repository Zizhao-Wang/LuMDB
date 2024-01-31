import numpy as np
from scipy.stats import genpareto, genextreme
import pandas as pd
from scipy.stats import zipf
from tqdm import tqdm
import argparse

parser = argparse.ArgumentParser(description='Generate Zipf distribution data.')
parser.add_argument('a_value', type=float, help='Value of a for Zipf distribution.')
parser.add_argument('num_keys', type=int, help='Number of keys to generate.')
args = parser.parse_args()

# 设定参数
num_keys = args.num_keys 
key_range = (1, num_keys)  
a_values = [] # 您想要测试的a值列表
a_values.append(args.a_value)

billion = 1000000000  # 1 Billion
file_size_in_billions = num_keys / billion  # 计算为Billion的数量
file_name = f'/home/wangzizhao/workloads/etc_keys{file_size_in_billions}B_zipf{args.a_value}.csv'

for a in tqdm(a_values): 
    keys = zipf.rvs(a, size = num_keys)
    # 组合成DataFrame
    data = pd.DataFrame({
        'Key': keys,
        # 'key_length': key_sizes,
        # 'Value': values,
        # 'val_length': value_sizes,
        # 'Operation': operations_col
    })
    # 保存到CSV文件，如果您需要不同的格式或者直接输出到屏幕，请调整这部分代码
    data.to_csv(f'/home/wangzizhao/workloads/zipf_keys{num_keys}_zipf{a}.csv', index=False)

# 打印前5条数据以检查
print(data.head())
