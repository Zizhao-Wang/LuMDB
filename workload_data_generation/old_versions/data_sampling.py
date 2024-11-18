import pandas as pd
from tqdm import tqdm

# 文件路径和数量
file_directory = '/wzz/SHCDB/workloads/'  # 存放文件的目录
num_files = 10000  # 文件数量

for file_index in tqdm(range(1, num_files + 1)):
    # 读取文件
    csv_file = f'{file_directory}etc_data{file_index}.csv'
    data = pd.read_csv(csv_file)
    keys = data['Key']
    
    # 初始化标签列表
    labeled = [0] * len(keys)

    # 遍历每个键，查看后续的200个键（包括当前键）
    for i in range(len(keys)):
        # 如果剩余键不足200个，则从头开始重新使用键
        if len(keys) - i < 200:
            window_keys = pd.concat([keys[i:], keys[:200 - (len(keys) - i)]])
        else:
            window_keys = keys[i:i + 200]

        # 计算窗口内键的频率并获取前20%的高频键
        frequency_of_keys = window_keys.value_counts()
        high_freq_threshold = max(1, int(len(frequency_of_keys) * 0.2))
        high_freq_keys = frequency_of_keys.head(high_freq_threshold).index

        # 为当前键设置标签
        labeled[i] = 1 if keys[i] in high_freq_keys else 0

    # 添加标签列到DataFrame并保存
    data['labels'] = labeled
    data.to_csv(csv_file, index=False)
