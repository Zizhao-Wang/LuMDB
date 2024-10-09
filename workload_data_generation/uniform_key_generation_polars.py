import numpy as np
import pandas as pd
from scipy.stats import zipf, uniform  # 添加 uniform 分布
import matplotlib.pyplot as plt
import csv
import os
from tqdm import tqdm  # 正确导入tqdm函数
import polars as pl

billion = 1000000000  # 1 Million for testing
million = 10000  # 1 Million for testing
num_keys = 1 * billion  
file_size_in_millions = 1  
key_size = 16
val_size = 128

# 生成均匀分布的数据
def generate_keys_uniform(num_keys, keys_file_name):
    keys = uniform.rvs(size=million, loc=0, scale=num_keys)  # 在 [0, num_keys] 范围内生成均匀分布数据
    keys = np.floor(keys).astype(int)  # 取整生成整数键
    data = pd.DataFrame({'Key': keys})
    data.to_csv(keys_file_name, index=False)
    print(f"Data for uniform distribution keys written to {keys_file_name}")

# 使用均匀分布生成数据
uniform_file_name = f'/home/jeff-wang/workloads/uniform_read_keys{key_size}_value{val_size}_{file_size_in_millions}M.csv'
generate_keys_uniform(num_keys, uniform_file_name)  # 生成均匀分布的数据
