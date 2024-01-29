import pandas as pd
import matplotlib.pyplot as plt

# 确保CSV文件路径是正确的
csv_file_path = '/wzz/SHCDB/workloads/etc_data1.csv'

# 读取CSV文件
data = pd.read_csv(csv_file_path)


# 指定x轴范围
x_min = data['Key'].min()
x_max = data['Key'].max()

key_counts = data['Key'].value_counts()

print(key_counts.head())

plt.figure(figsize=(14, 7))
plt.title('Key Density Trend')
plt.xlabel('Key')
plt.ylabel('Density')
plt.xlim(-10, x_max)  # 设置x轴范围
plt.ylim(1,40000)
plt.plot(key_counts.index, key_counts.values)
plt.grid(True)

# 保存为PNG文件
plt.savefig('key_density_trend.png')
