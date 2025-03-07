import pandas as pd

# 读取 CSV 文件
file_path = '/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot/pm_monitor.lpg'  # 请替换为实际的文件路径
df = pd.read_csv(file_path)

# 查看数据框的前几行，检查数据
print(df.head())

# 可以选择保存为新的 CSV 文件，或者进行其他操作
new_file_path = '/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/prismdb_scripts/10B_prismdb_zipf_hot/processed_log.csv'  # 处理后的新文件路径
df.to_csv(new_file_path, index=False)

# 打印出可读的格式
print(df)
