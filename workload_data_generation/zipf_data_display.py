import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

# 假设我们已经有了一个名为'data'的DataFrame，并且它包含了我们需要绘制的'Key'列。
# data = pd.read_csv('path_to_your_csv.csv')  # 实际使用时请取消注释，并确保路径正确。

# 为了展示如何生成图形，这里我们随机生成一些符合Zipf分布的数据作为示例
zipf_data = np.random.zipf(a=1.08, size=1000)

# 使用matplotlib生成密度图
plt.figure(figsize=(10, 6))
plt.hist(zipf_data, bins=30, density=True)
plt.title('Density Plot of Zipf Distribution')
plt.xlabel('Value')
plt.ylabel('Density')
plt.grid(True)
plt.savefig('density_plot.png')  # 保存图形为PNG文件
