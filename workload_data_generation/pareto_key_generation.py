import math
import numpy as np
import matplotlib.pyplot as plt

def pareto_cdf_inversion(u, theta, k, sigma):
    """
    生成符合帕累托分布的整数数据点。

    参数:
    - u (float): 均匀分布的随机数，0 < u < 1
    - theta (float): 位置参数，分布的起始点
    - k (float): 形状参数，控制尾部行为
    - sigma (float): 尺度参数，控制分布的扩展程度

    返回:
    - int: 向上取整后的帕累托分布数据点
    """
    if k == 0.0:
        ret = theta - sigma * math.log(u)
    else:
        ret = theta + sigma * (math.pow(u, -k) - 1) / k
    return int(math.ceil(ret))

def generate_pareto_data(n, theta, k, sigma, seed=None):
    """
    生成n个符合帕累托分布的整数数据点。

    参数:
    - n (int): 生成的数据点数量
    - theta (float): 位置参数
    - k (float): 形状参数
    - sigma (float): 尺度参数
    - seed (int, 可选): 随机种子，确保结果可重复

    返回:
    - list of int: 生成的数据点列表
    """
    if seed is not None:
        np.random.seed(seed)
    
    # 生成n个均匀分布的随机数
    u = np.random.uniform(0, 1, n)
    
    # 使用向量化操作生成数据点
    data = np.where(
        k == 0.0,
        theta - sigma * np.log(u),
        theta + sigma * (np.power(u, -k) - 1) / k
    )
    
    # 向上取整并转换为整数
    data = np.ceil(data).astype(int)
    
    return data.tolist()

def main():
    # 参数设定（可以根据需要调整）
    theta = 0.0    # 位置参数
    k = 0.2615     # 形状参数
    sigma = 25.45  # 尺度参数
    n = 1000       # 生成的数据点数量
    seed = 42      # 随机种子（可选）

    # 生成数据
    pareto_data = generate_pareto_data(n, theta, k, sigma, seed)

    # 打印前10个数据点
    print("前10个生成的数据点:", pareto_data[:10])

    # 绘制直方图
    plt.figure(figsize=(10, 6))
    plt.hist(pareto_data, bins=50, density=True, alpha=0.6, color='skyblue', edgecolor='black')

    # 绘制理论帕累托分布的PDF
    x = np.linspace(theta, max(pareto_data), 1000)
    if k == 0.0:
        pdf = (1 / sigma) * np.exp(-(x - theta) / sigma)
    else:
        pdf = (1 / sigma) * np.power(1 + k * (x - theta) / sigma, -(1 / k + 1))
    plt.plot(x, pdf, 'r-', lw=2, label='理论PDF')

    plt.title('符合帕累托分布的数据生成与可视化')
    plt.xlabel('数据值')
    plt.ylabel('密度')
    plt.legend()
    plt.grid(True)
    plt.show()

if __name__ == "__main__":
    main()
