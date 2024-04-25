import numpy as np
from scipy.stats import genpareto, genextreme
import pandas as pd
from scipy.stats import zipf
from tqdm import tqdm
import argparse
import os
import sys


# 设定参数
num_keys = 1000  
a_values = [1.1] # 您想要测试的a值列表

billion = 1000  # 1 Billion 000000
file_size_in_billions = num_keys / billion  # 计算为Billion的数量
file_name = f'/home/jeff-wang/workloads/zipf_keys{file_size_in_billions}k_zipf1.1.csv'

if os.path.exists(file_name):
    print(f"File {file_name} already exists. Exiting program.")
    sys.exit()

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
    data.to_csv(file_name, index=False)

# 打印前5条数据以检查
print(data.head())
