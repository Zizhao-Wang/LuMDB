import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
import numpy as np
import pandas as pd

# -------------------------------
# 全局字体大小设置为35，类似于 gnuplot font 'Times-New-Roman,35'
# plt.rcParams["font.size"] = 35

# 检查是否安装了 Times New Roman，并设置字体
available_fonts = [f.name for f in fm.fontManager.ttflist]
if "Times New Roman" in available_fonts:
    plt.rcParams["font.family"] = "Times New Roman"
else:
    print("Times New Roman not found, using 'Liberation Serif' as fallback.")
    plt.rcParams["font.family"] = "Liberation Serif"
plt.rcParams["axes.unicode_minus"] = False  # 确保负号正常显示
# -------------------------------

# 读取 CSV 文件（请根据实际路径修改）
df = pd.read_csv('/home/jeff-wang/LuMDB/colddatawrite.csv')

# -------------------------------
# 选择需要展示的行（0-based index），例如展示第1,3,5,7,9行（索引 [0,2,4,6,8]）
selected_rows = [0, 2, 4, 6, 8, 10]
df = df.iloc[selected_rows].reset_index(drop=True)
# -------------------------------

# -------------------------------
# 定义每个分组的列名（每组2列，共4组）
group_names = [
    ("Zipf1.1-Toatl",   "Zipf1.1-user"), 
    ("Zipf1.3-Toatl",   "Zipf1.3-user"), 
    ("cluster40-Toatl", "cluster40-user"), 
    ("cluster49-Toatl", "cluster49-user")
]

# 定义每个柱子上半部分（total ios）的颜色（十六进制）
total_colors = [
    "#F37251",  # Group 1
    "#F7A593",  # Group 2
    "#B4D6E4",  # Group 3
    "#6AA4C6"   # Group 4
]
# 定义下半部分（user ios）颜色，选择与上面的颜色形成明显对比的红色
user_color = "#16F65B"  # 深红色
# -------------------------------

num_groups = len(group_names)     # 分组数 (4)
num_points = len(df)              # 展示点数
x = np.arange(num_points)         # x 轴坐标: 0,1,2,...,num_points-1
bar_width = 0.15

# -------------------------------
# 设置图形尺寸为8×6英寸 (与 gnuplot 的 size 8.0,6.0 相同)
fig, ax = plt.subplots(figsize=(8, 6))
# -------------------------------

# 计算每组柱子的水平偏移量，使它们在同一 x 点上并排显示
offsets = np.linspace(-bar_width * (num_groups - 1) / 2,
                       bar_width * (num_groups - 1) / 2,
                       num_groups)

# 绘制堆叠柱状图，其中下层（user ios）统一为 user_color，上层（total ios）使用各自的 total_color
for i, (col_total, col_user) in enumerate(group_names):
    offset = offsets[i]
    y_total = df[col_total]
    y_user  = df[col_user]
    
    # 绘制下半部分（user ios）
    ax.bar(x + offset, y_user, bar_width,
           color=user_color,
           edgecolor='black',
           linewidth=1)
    
    # 绘制上半部分（total ios）堆叠在 user ios 上
    ax.bar(x + offset, y_total, bar_width,
           bottom=y_user,
           color=total_colors[i],
           edgecolor='black',
           linewidth=1)

# 设置 X 和 Y 轴标签及图表标题
ax.set_xlabel('Points')
ax.set_ylabel('Values')
ax.set_title('Stacked Bar Chart: 4 Groups, Selected Points')

# 设置 X 轴刻度及标签，例如 "Point 1", "Point 2", ...
ax.set_xticks(x)
ax.set_xticklabels([f'Point {i+1}' for i in x])

# 设置网格线（横纵方向均显示，宽度1.0，实线，灰色）
ax.grid(True, which='both', linestyle='-', linewidth=1.0, color='gray')

# 确保柱子从 y=0 开始，且无额外边距
ax.set_ylim(bottom=0)
ax.margins(y=0)

plt.tight_layout()
plt.savefig('/home/jeff-wang/LuMDB/cold_data_writesin_compaction.pdf', format='pdf')
plt.show()
