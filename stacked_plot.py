import matplotlib.pyplot as plt
import numpy as np

# 创建数据
points = 10
categories = 2  # 每个堆叠柱子有2个部分
groups = 4  # 每个点有4个柱子（2个堆叠柱子）

# 随机数据生成
data = np.random.randint(1, 10, size=(points, categories * groups))

# 定义柱子的宽度和位置
bar_width = 0.35
index = np.arange(points)

# 创建堆叠图
fig, ax = plt.subplots(figsize=(12, 6))

# 每个堆叠柱子分配2个类别的堆叠
bottom = np.zeros(points)

for i in range(groups):
    # 为每个堆叠柱子绘制2部分
    ax.bar(index + i * bar_width, data[:, 2 * i], bar_width, bottom=bottom, label=f'Group {i+1} - Part 1')
    bottom += data[:, 2 * i]  # 更新底部

    ax.bar(index + i * bar_width, data[:, 2 * i + 1], bar_width, bottom=bottom, label=f'Group {i+1} - Part 2')
    bottom += data[:, 2 * i + 1]  # 更新底部

# 设置标签和标题
ax.set_xlabel('Points')
ax.set_ylabel('Values')
ax.set_title('Stacked Bar Chart with 4 bars per point, each with 2 parts stacked')

# 设置x轴的ticks和标签
ax.set_xticks(index + bar_width * 1.5)
ax.set_xticklabels([f'Point {i+1}' for i in range(points)])

# 添加图例
ax.legend()

# 调整布局
plt.tight_layout()

# 保存为PDF文件
plt.savefig('sta.pdf', format='pdf')

# 显示图形
plt.show()
