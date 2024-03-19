import matplotlib.pyplot as plt
from tqdm import tqdm  # 正确导入tqdm函数
import polars as pl

  
a_values = [1.01]  # 
percents = [1, 5, 10, 15, 20, 25, 30]   # 


billion = 1000000000  # 1 Billion
# billion = 10000  # 1 Billion
num_keys = 10 * billion  # 5 Billion
file_size_in_billions = num_keys / billion  # 计算为Billion的数量


def generate_and_process_data(a_value, percents, key_file_name, file_size_in_billions):
    # 读取数据使用polars
    keys = pl.read_csv(key_file_name, separator=",")
    key_frequencies = keys.group_by("Key").agg(pl.len().alias("Frequency"))
    key_frequencies_sorted = key_frequencies.sort("Frequency", descending=True)
    
    total_keys = key_frequencies_sorted.height
    print(f"Data for Zipf parameter {a_value} read from {key_file_name} with a total of {total_keys} KV pairs.")

    # 对每个百分比处理数据
    for percent in percents:
        top_n_keys = int(total_keys * (percent / 100))
        top_n = key_frequencies_sorted.head(top_n_keys)
        key_fre_file_name = f'/home/jeff-wang/workloads/zipf{a}_top{percent}_keys{file_size_in_billions}B.csv'
        
        # 写入top N% keys到文件
        top_n.write_csv(key_fre_file_name)
        print(f"Top {percent}% keys written to {key_fre_file_name} with {top_n_keys} hot KV pairs!\n")
 

for a in tqdm(a_values, desc="Processing zipf values"):
    file_name = f'/home/jeff-wang/workloads/zipf{a}_keys10.0B.csv'
    generate_and_process_data(a, percents, file_name, file_size_in_billions)
