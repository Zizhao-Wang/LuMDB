import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
import matplotlib
from matplotlib import font_manager
from matplotlib.colors import ListedColormap, LinearSegmentedColormap

# 自定义的颜色映射函数
def generate_custom_cmap():
    # 定义从绿色到红色的颜色序列
    colors = [
        # "#d4ffc6", # 浅绿色
        # "#aedf8f", # 中等绿色
        # "#87cc5c", # 较深绿色
        # "#6cbf30", # 深绿色
        # "#f7fcb9", # 淡黄色
        # "#fde0ac", # 浅橙色
        # "#fdae61", # 橙色
        # "#f46d43", # 深橙色
        # "#d73027", # 浅红色
        # "#a50026", # 深红色
        "#fdf181",
        "#faf6ae",
        "#e5eca9",
        "#e3f2b1",
        "#d9ebc1",
        "#cce4cc",
        "#bbded0",
        "#b0ded4",
        "#b5dbd0",
        "#afdbd1",
        "#aedad1",
        "#8fb5b6",
        "#81b6bb",
        "#86a6bc",
    ]
    return ListedColormap(colors)


dictionary = {
    1: "read",
    2: "write"
}

datas=[1,2]

for data_co in datas:
    matplotlib.rcParams['font.family'] = 'Times New Roman'
    matplotlib.rcParams['font.size'] = 12

    # 创建一个从绿色到红色的颜色映射
    # colors = ['#00A000', '#A00000']  # green to red
    # cmap = LinearSegmentedColormap.from_list("custom_green_red", colors, N=256)

    data_matrix = np.zeros((5, 1000))  # 5个level，1000个数据点

    # 读取数据
    for i in range(5):
        filename = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/extract_extra_data_floder/zipf1.1/outputfile{i+1}.txt'
        data = np.loadtxt(filename, usecols=data_co)  # 假设数据在每个文件的第二列
        data_matrix[i, :] = data  # 将数据存入对应的列


# 绘制热图
    custom_cmap = generate_custom_cmap()
    plt.figure(figsize=(8, 6))
    plt.subplots_adjust(left=0.1, right=0.9, top=0.95, bottom=0.1) 
    
    # # 添加网格以模拟边框
    # plt.grid(which="minor", color="black", linestyle='-', linewidth=0.25)
    # plt.minorticks_on()

    # # 根据数据点的数量调整网格线位置
    # plt.xticks(np.arange(-0.5, 1000, 1), minor=True)
    # plt.yticks(np.arange(-0.5, 5, 1), minor=True)

    im = plt.imshow(data_matrix, aspect='auto', interpolation='nearest', cmap=custom_cmap)

    # 这会隐藏边缘的网格线
    plt.tick_params(which="minor", size=0)
    plt.colorbar(im, label='Intensity (MB)')
    plt.xlabel('Written data volume (10 Million)')
    plt.ylabel('Level')
    plt.xticks(np.linspace(0, 999, 5), np.linspace(1, 1000, 5).astype(int))  # 设置x轴标签
    plt.yticks(range(5), [f'L {i+1}' for i in range(5)])  # 设置y轴标签
    plt.savefig(f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/extract_extra_data_floder/zipf1.1/extra_{dictionary[data_co]}_heatmap.pdf', format='pdf')
