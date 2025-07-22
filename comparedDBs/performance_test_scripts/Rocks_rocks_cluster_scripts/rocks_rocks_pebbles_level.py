import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle
import matplotlib.patches as mpatches
import os

# 设置中文字体
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams['font.size'] = 14

# 示例数据 - 请根据你的实际数据文件调整
# 假设数据格式：每行对应一个T值(2,5,10,30,50)，每列对应一个系统(LuMDB, 2PLaT, PLaT)
data_low = np.array([
    [0.15, 0.12, 0.10],  # T=2
    [0.18, 0.15, 0.13],  # T=5
    [0.22, 0.18, 0.16],  # T=10
    [0.25, 0.21, 0.19],  # T=30
    [0.26, 0.22, 0.20],  # T=50
])

data_high = np.array([
    [0.8, 0.7, 0.6],     # T=2
    [1.0, 0.9, 0.8],     # T=5
    [1.2, 1.1, 1.0],     # T=10
    [1.4, 1.3, 1.2],     # T=30
    [1.51, 1.4, 1.3],    # T=50
])

# 创建图形和子图
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)

# 颜色定义
colors = ['#F37251', '#F7A593', '#B4D6E4']
labels = ['LuMDB', '2PLaT', 'PLaT']

# T值和位置
t_values = ['2', '5', '10', '30', '50']
x_positions = np.arange(len(t_values))
width = 0.25

# 绘制上半部分图（高偏斜数据）
for i, (color, label) in enumerate(zip(colors, labels)):
    ax1.bar(x_positions + i*width, data_high[:, i], width, 
            color=color, label=label if i == 0 else "")

# 绘制下半部分图（低偏斜数据）
for i, (color, label) in enumerate(zip(colors, labels)):
    ax2.bar(x_positions + i*width, data_low[:, i], width, 
            color=color, label=label)

# 设置Y轴范围
ax1.set_ylim(0.5, 1.6)  # 高值范围
ax2.set_ylim(0, 0.3)    # 低值范围

# 隐藏子图间的边框
ax1.spines['bottom'].set_visible(False)
ax2.spines['top'].set_visible(False)
ax1.xaxis.tick_top()
ax1.tick_params(labeltop=False)
ax2.xaxis.tick_bottom()

# 添加截断线
d = 0.015  # 截断线的大小
kwargs = dict(transform=ax1.transAxes, color='k', clip_on=False)
ax1.plot((-d, +d), (-d, +d), **kwargs)        # 左下
ax1.plot((1 - d, 1 + d), (-d, +d), **kwargs)  # 右下

kwargs.update(transform=ax2.transAxes)
ax2.plot((-d, +d), (1 - d, 1 + d), **kwargs)  # 左上
ax2.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)  # 右上

# 设置标签和标题
ax2.set_xlabel('T', fontsize=16)
ax1.set_ylabel('TWI in high skewed workload (MiB)', fontsize=14)
ax2.set_ylabel('TWI in low skewed workload (MiB)', fontsize=14)

# 设置X轴标签
ax2.set_xticks(x_positions + width)
ax2.set_xticklabels(t_values)

# 添加图例
ax1.legend(loc='upper left', bbox_to_anchor=(0, 1), ncol=3, fontsize=12)

# 添加网格
ax1.grid(True, alpha=0.3)
ax2.grid(True, alpha=0.3)

# 添加数据集标识
ax1.text(0.02, 0.95, 'High(1.3)', transform=ax1.transAxes, 
         fontsize=14, fontweight='bold', verticalalignment='top')
ax2.text(0.02, 0.95, 'Low(1.1)', transform=ax2.transAxes, 
         fontsize=14, fontweight='bold', verticalalignment='top')

plt.tight_layout()
plt.subplots_adjust(hspace=0.05)

# 保存图片
script_dir = os.path.dirname(os.path.abspath(__file__))
output_path = os.path.join(script_dir, 'TWI_under_different_datastructure_as_skewnewss.pdf')

# 保存图片到脚本所在目录
plt.savefig(output_path, bbox_inches='tight', dpi=300, format='pdf')
print(f"图片已保存到: {output_path}")

# 如果你有数据文件，可以使用以下代码读取：
"""
# 读取数据文件的示例代码
import pandas as pd

# 读取1.1.dat文件
data_low = pd.read_csv('1.1.dat', sep='\s+', header=None).values

# 读取1.3.dat文件  
data_high = pd.read_csv('1.3.dat', sep='\s+', header=None).values
"""