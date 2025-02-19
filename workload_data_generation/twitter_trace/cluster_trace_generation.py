import pandas as pd

# Step 1: 读取原始文件
file_path = '/mnt/nvm/cluster14.sort'  # 原始文件路径
# file_path = '/home/jeff-wang/cache-trace/samples/2020Mar/cluster047'
df = pd.read_csv(file_path, header=None)  # 读取文件并忽略列名

# 输出读取的数据文件内容（前几行）
print("Step 1: Loaded data from cluster008.csv")
print(df.head(5))

# Step 2: 统计每个key的出现次数
key_counts = df[1].value_counts()  # 统计第二列每个key出现的次数

# 输出key出现次数的统计信息
print("\nStep 2: Key counts")
print(key_counts.head(5))

# Step 3: 对key按出现次数进行排名
key_rank = key_counts.rank(method='first', ascending=False).astype(int)  # 计算排名

# 输出排名信息
print("\nStep 2.1: Key ranks")
print(key_rank.head())

# 新增：统计出现次数大于等于2次和仅出现1次的key的数量
count_gte_2 = (key_counts >= 2).sum()  # 出现次数大于等于2次的key的个数
count_1 = (key_counts == 1).sum()  # 出现次数为1次的key的个数

# 输出统计结果
print(f"\nStep 2.1: Statistics")
print(f"Number of keys with occurrences >= 2: {count_gte_2}")
print(f"Number of keys with occurrences = 1: {count_1}")

# 输出排名信息
print("\nStep 3: Key ranks")
print(key_rank.head())

# Step 4: 创建哈希表 (字典)，key为key，value为对应的rank
rank_dict = dict(zip(key_counts.index, key_rank))  # 创建哈希表，key -> rank

# 输出哈希表的前几个映射
print("\nStep 4: Key to rank mapping (hash table)")
for key, rank in list(rank_dict.items())[:10]:
    print(f"Key: {key}, Rank: {rank}")

# Step 5: 复制原始文件，生成新文件
df_copy = df.copy()  # 复制原始数据框

# 用哈希表中的rank替换key
df_copy[1] = df_copy[1].map(rank_dict)  # 将第二列的key替换为对应的排名

# 输出替换后的数据内容（前几行）
print("\nStep 5: Replaced keys with ranks in the copy")
print(df_copy.head())

# 保存新的数据到second_cluster008.csv文件
second_file_path = '/mnt/nvm/second_cluster14.sort'  # 新文件路径
df_copy.to_csv(second_file_path, header=False, index=False)  # 保存为新的文件

# 输出新文件路径和部分内容查看
print(f"\nNew file saved at {second_file_path}")
print(df_copy.head())  # 打印前几行查看结果
