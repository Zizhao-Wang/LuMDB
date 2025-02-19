import pandas as pd
import matplotlib.pyplot as plt

# 读取数据文件
file_path = '/home/jeff-wang/cache-trace/samples/2020Mar/cluster008.csv'  # 替换为实际文件路径

# 假设文件中的数据以逗号分隔，且每行有 7 列
df = pd.read_csv(file_path, header=None)

# 提取第二列，也就是key
keys = df[1]

# 统计每个key的出现次数
key_counts = keys.value_counts()

# 对key按出现次数进行排名
key_rank = key_counts.rank(method='first', ascending=False).astype(int)

# 将排名数据添加到一个新的DataFrame中
ranked_keys = pd.DataFrame({
    'key': key_counts.index,
    'count': key_counts.values,
    'rank': key_rank.values
})

# 将数据保存到一个新的文件（cluster008）
ranked_keys.to_csv('cluster008.csv', index=False)

# 打印排名前10的key及其出现次数
print(ranked_keys.head(10))

# 绘制折线图，展示每个key的出现次数
plt.figure(figsize=(10, 6))
plt.plot(ranked_keys['key'], ranked_keys['count'], marker='o', linestyle='-', color='b')
plt.title('Key Distribution Frequency')
plt.xlabel('Key')
plt.ylabel('Frequency')
plt.xticks(rotation=90)  # 旋转X轴标签，以免重叠
plt.grid(True)
plt.show()
# Save the plot as an image in the current directory
plt.savefig('./twitter_trace_key_distribution018.png')

# Display a message indicating the image is saved
print("Plot saved as 'twitter_trace_key_distribution.png'")