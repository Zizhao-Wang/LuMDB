from scipy.stats import zipf
from tqdm import tqdm  # 正确导入tqdm函数
import polars as pl

  
a_values = [1.5]  #  1.2, 1.3, 1.4
percents = [100]   # 5, 10, 15, 20, 25, 30


billion = 1000000000  # 1 Billion
# billion = 10000  # 1 Billion
num_keys = 10 * billion  # 5 Billion
file_size_in_billions = num_keys / billion  # 计算为Billion的数量
# file_name = f'/home/jeff-wang/workloads/zipf{args.a_value}_keys{file_size_in_billions}B.csv'
# key_fre_file_name = f'/home/jeff-wang/workloads/zipf{args.a_value}_hotkeys{file_size_in_billions}B.csv'

def generate_keys(a_value, num_keys):
    keys = zipf.rvs(a_value, size = num_keys)
    data = pl.DataFrame({
        'Key': keys,
    })
    data.write_csv(file_name)
    print(f"Data for Zipf parameter {a_value} keys written to {keys_file_name}")


for a in tqdm(a_values, desc="Processing zipf values"):
    file_name = f'/home/jeff-wang/workloads/zipf{a}_keys{file_size_in_billions}B.csv'
    generate_keys(a, num_keys, file_name)