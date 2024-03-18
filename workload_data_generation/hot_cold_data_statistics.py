import pandas as pd
import csv
import os
import numpy as np
from scipy.stats import genpareto, genextreme
import matplotlib.pyplot as plt

def load_data(csv_file_path):
    """
    load CSV file
    :param csv_file_path: CSV文件的路径
    :return: DataFrame对象
    """
    return pd.read_csv(csv_file_path)

def calculate_top_n_percent_keys(data, percent):
    """
    Calculate the top N% most frequent keys
    :param data: DataFrame object containing the 'Key' columns
    :param percent: Percentage of keys to be fetched
    :return: Series object containing the top percent% most frequent keys and their frequencies
    """
    key_frequencies = data['Key'].value_counts()
    print(f"There are {len(key_frequencies)} KV pairs and {int(len(key_frequencies) * (percent / 100))} hot KV pairs!\n")
    top_n = int(len(key_frequencies) * (percent / 100))
    return key_frequencies.head(top_n)
    

def plot_key_frequencies(keys_frequencies, percent,a):
    """
    Plotting the frequency distribution of keys
    :param keys_frequencies: Series object of the most frequent keys and their frequencies
    :param percent: Percentage of keys to be fetched
    """
    plt.figure(figsize=(10, 6))
    keys_frequencies.plot(kind='bar')
    plt.title(f'Distribution of top {percent}% keys with highest frequency')
    plt.xlabel('Key')
    plt.ylabel('frequency')
    plt.savefig(f'hotkey{percent}_distribution_zipf{a}.png')

def write_to_file(top_n_keys, output_file_path):
    # 获取文件扩展名
    _, file_extension = os.path.splitext(output_file_path)

    if file_extension.lower() == '.csv':
        # 用csv方式写入
        with open(output_file_path, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(['Key', 'Frequency'])  # 写入表头
            for key, frequency in top_n_keys.items():
                writer.writerow([key, frequency])
    elif file_extension.lower() == '.txt':
        # 用txt方式写入
        with open(output_file_path, 'w') as file:
            for key, frequency in top_n_keys.items():
                file.write(f"{key},{frequency}\n")
    else:
        print(f"Unsupported file format: {file_extension}")

    print(f"Data written to {output_file_path}")

def main(csv_file_path, percent, output_file_path='hot_keys.csv', a= 1.01):
    data = load_data(csv_file_path)
    top_n_percent_keys = calculate_top_n_percent_keys(data, percent)
    print(f"The top {percent}% keys with the highest frequency and their frequency:")
    print(top_n_percent_keys) 
    plot_key_frequencies(top_n_percent_keys, percent,a)
    write_to_file(top_n_percent_keys, output_file_path)

a_values = [1.01, 1.1, 1.2, 1.3, 1.4] 
percents = [1, 5, 10, 15, 20, 25, 30]  # Getting the top 1% most frequent keys

for a in a_values:
    for percent in percents:
        csv_file_path = f'/home/jeff-wang/workloads/etc_data_zipf{a}.csv' 
        output_file_path1 = f'/home/jeff-wang/workloads/etc_output_file{a}.csv'
        main(csv_file_path,percent,output_file_path1, a)
