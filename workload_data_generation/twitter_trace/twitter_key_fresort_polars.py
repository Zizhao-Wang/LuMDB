import matplotlib.pyplot as plt
from tqdm import tqdm  # 正确导入tqdm函数
import polars as pl

  
a_values = [40]  # 
percents = [10]   # 


billion = 1000000000  # 1 Billion
# billion = 10000  # 1 Billion
num_keys = 10 * billion  # 5 Billion
file_size_in_billions = num_keys / billion  # 计算为Billion的数量


def generate_and_process_data(a_value, percents, key_file_name, file_size_in_billions):
    # Read data with proper column names


    keys = pl.read_csv(key_file_name, separator=",", has_header=False, n_rows=1100000000)
    
    # Manually assign column names
    keys.columns = ["col_1", "col_2", "col_3", "col_4", "col_5", "col_6", "col_7"]  # Adjust according to the actual columns
    
    # # Filter rows where the 6th column is "set"
    # set_keys = keys.filter(pl.col("col_6") == "set")
    set_keys = keys
    
    # Only use the second column (key) to calculate frequencies
    key_frequencies = set_keys.group_by("col_2").agg(pl.len().alias("Frequency"))
    
    # Sort by frequency in descending order
    key_frequencies_sorted = key_frequencies.sort("Frequency", descending=True)
    
    total_keys = key_frequencies_sorted.height
    print(f"Data for Zipf parameter {a_value} read from {key_file_name} with a total of {total_keys} KV pairs.")

    # Process data for each percentage
    for percent in percents:
        top_n_keys = int(total_keys * (percent / 100))
        top_n = key_frequencies_sorted.head(top_n_keys)
        key_fre_file_name = f'/mnt/nvm/cluster{a_value}_top{percent}_keys{file_size_in_billions}B.csv'
        
        # Write top N% keys to file
        top_n.write_csv(key_fre_file_name)
        print(f"Top {percent}% keys written to {key_fre_file_name} with {top_n_keys} hot KV pairs!\n")
 

for a in tqdm(a_values, desc="Processing zipf values"):
    file_name = f'/mnt/nvm/second_cluster{a}.sort'
    generate_and_process_data(a, percents, file_name, file_size_in_billions)
