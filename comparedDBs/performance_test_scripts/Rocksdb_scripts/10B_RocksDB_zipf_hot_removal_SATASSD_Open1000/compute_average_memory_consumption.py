import re
import os

# 文件路径
# file_path = '/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_write_open1000/leveldb_memory_usage_key16val128_zipf1.1_memory64MiB.log'
# 获取当前脚本所在的目录
current_dir = os.path.dirname(os.path.abspath(__file__))
file_name = 'RocksDB_memoryusage_10B_key16_val128_wbs64MB_zipf1.3.log'
# 拼接成完整路径
absolute_path = os.path.join(current_dir, file_name)

# 正则表达式：提取每行中的 "Resident set size" 值（以MB为单位）
pattern = r"Resident set size:\s([\d\.]+)\sMB"

# 存储所有提取的 "Resident set size" 数值
resident_sizes = []

# 以只读模式打开文件
with open(absolute_path, 'r') as file:
    for line in file:
        # 使用正则表达式查找每行中的Resident set size
        match = re.search(pattern, line)
        if match:
            # 将匹配到的值添加到列表中
            resident_sizes.append(float(match.group(1)))

# 如果找到了Resident set size的值，计算并输出平均值
if resident_sizes:
    average_resident_size = sum(resident_sizes) / len(resident_sizes)
    print(f"Average Resident set size: {average_resident_size} MB")
else:
    print("No Resident set size data found in the file.")
