import numpy as np
import pandas as pd
from scipy.stats import zipf
import matplotlib.pyplot as plt
import csv
import os
from tqdm import tqdm  # 正确导入tqdm函数
import polars as pl

  
a_values = [1.02]  #  1.2, 1.3, 1.4
percents = [100]   # 5, 10, 15, 20, 25, 30


billion = 1000  # 1 Billion
# billion = 10000  # 1 Billion
num_keys = 10 * billion  # 5 Billion
file_size_in_billions = num_keys / billion  # 计算为Billion的数量
# file_name = f'/home/jeff-wang/workloads/zipf{args.a_value}_keys{file_size_in_billions}B.csv'
# key_fre_file_name = f'/home/jeff-wang/workloads/zipf{args.a_value}_hotkeys{file_size_in_billions}B.csv'


def generate_keys(a_value, num_keys, keys_file_name):
    keys = zipf.rvs(a_value, size = num_keys)
    # 组合成DataFrame
    data = pd.DataFrame({
        'Key': keys,
    })
    data.to_csv(file_name, index=False)
    print(f"Data for Zipf parameter {a_value} keys written to {keys_file_name}")

def generate_and_process_data(a_value, num_keys, percent, key_file_name, key_fre_file_name):
    
    # read data using pandas
    keys = pd.read_csv(key_file_name)
    key_frequencies = keys['Key'].value_counts()

    # 计算Top N% keys
    top_n = int(len(key_frequencies) * (percent / 100))
    top_n_keys = key_frequencies.head(top_n)

    with open(key_fre_file_name, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['Key', 'Frequency'])
        for key, frequency in top_n_keys.items():
            writer.writerow([key, frequency])


    # # read data using polars
    # keys = pl.read_csv(key_file_name, separator=",")
    # key_frequencies = keys.group_by("Key").agg(pl.len().alias("Frequency"))
    # key_frequencies_sorted = key_frequencies.sort("Frequency", descending=True)

    # # 计算Top N% keys
    # total_keys  = key_frequencies_sorted.height
    # top_n_keys = int(total_keys * (percent / 100))
    # top_n = key_frequencies_sorted.head(top_n_keys)

    # top_n.write_csv(key_fre_file_name)
    # print(f"Data for Zipf parameter {a_value} read data from {key_file_name} and top {percent}% keys written to {key_fre_file_name}")

    # 可视化
    # plt.figure(figsize=(10, 6))
    # top_n_keys.plot(kind='bar')
    # plt.title(f'Distribution of top {percent}% keys with highest frequency, Zipf parameter {a_value}')
    # plt.xlabel('Key')
    # plt.ylabel('Frequency')
    # plt.savefig(f'hotkey{percentyigon}_distribution_zipf{a_value}.png')
    # plt.close()

    # # 如果这是第一次百分比处理，保存所有键
    # if percent == percents[0]:
    #     data.to_csv(key_file_name, index=False)
    #     print(f"Data for Zipf parameter {a_value} keys written to {key_file_name}")
 


for a in tqdm(a_values, desc="Processing zipf values"):
    file_name = f'/home/jeff-wang/workloads/zipf{a}_keys{file_size_in_billions}B.csv'
    # generate_keys(a, num_keys, file_name)
    for percent in tqdm(percents, desc=f"Processing percents for a={a}"):
        key_fre_file_name = f'/home/jeff-wang/workloads/zipf{a}_2top{percent}_keys{file_size_in_billions}B.csv'
        generate_and_process_data( a, num_keys, percent, file_name, key_fre_file_name)
