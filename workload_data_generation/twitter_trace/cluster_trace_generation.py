import pandas as pd
import logging

# Step 1: 全局定义文件路径
CLUSTER_NUM = 1 # 这里可以修改为任何需要的 cluster 数字
file_path = f'/mnt/nvm/cluster{CLUSTER_NUM}.sort'  # 根据全局定义的 CLUSTER_NUM 来确定文件路径

# 定义输出日志文件路径
log_file_path = f'/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_TwitterCluster_workloads/cluster{CLUSTER_NUM}_information.txt'  # 你可以自定义日志文件的路径

# 配置日志记录器
logging.basicConfig(filename=log_file_path, level=logging.INFO, 
                    format='%(asctime)s - %(levelname)s - %(message)s')

# Step 2: 读取原始文件
df = pd.read_csv(file_path, header=None, nrows=500000000)  # 读取文件并忽略列名

# 输出读取的数据文件内容（前几行）
logging.info("Step 1: Loaded data from cluster%s.sort", CLUSTER_NUM)
logging.info(df.head(5))

# Step 3: 统计每个key的出现次数
key_counts = df[1].value_counts()  # 统计第二列每个key出现的次数

# 输出key出现次数的统计信息
logging.info("\nStep 2: Key counts")
logging.info(key_counts.head(5))

# Step 4: 对key按出现次数进行排名
key_rank = key_counts.rank(method='first', ascending=False).astype(int)  # 计算排名

# 输出排名信息
logging.info("\nStep 2.1: Key ranks")
logging.info(key_rank.head())

# 新增：统计出现次数大于等于2次和仅出现1次的key的数量
count_gte_2 = (key_counts >= 2).sum()  # 出现次数大于等于2次的key的个数
count_1 = (key_counts == 1).sum()  # 出现次数为1次的key的个数

# 输出统计结果
logging.info(f"\nStep 2.1: Statistics")
logging.info(f"Number of keys with occurrences >= 2: {count_gte_2}")
logging.info(f"Number of keys with occurrences = 1: {count_1}")

# 输出排名信息
logging.info("\nStep 3: Key ranks")
logging.info(key_rank.head())

# Step 5: 创建哈希表 (字典)，key为key，value为对应的rank
rank_dict = dict(zip(key_counts.index, key_rank))  # 创建哈希表，key -> rank

# 输出哈希表的前几个映射
logging.info("\nStep 4: Key to rank mapping (hash table)")
for key, rank in list(rank_dict.items())[:10]:
    logging.info(f"Key: {key}, Rank: {rank}")

# Step 6: 复制原始文件，生成新文件
df_copy = df.copy()  # 复制原始数据框

# 用哈希表中的rank替换key
df_copy[1] = df_copy[1].map(rank_dict)  # 将第二列的key替换为对应的排名

# 输出替换后的数据内容（前几行）
logging.info("\nStep 5: Replaced keys with ranks in the copy")
logging.info(df_copy.head())

# 保存新的数据到second_cluster.csv文件
second_file_path = f'/mnt/nvm/second_cluster{CLUSTER_NUM}.sort'  # 新文件路径
df_copy.to_csv(second_file_path, header=False, index=False)  # 保存为新的文件

# 输出新文件路径和部分内容查看
logging.info(f"\nNew file saved at {second_file_path}")
logging.info(df_copy.head())  # 打印前几行查看结果
