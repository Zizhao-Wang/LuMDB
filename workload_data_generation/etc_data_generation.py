import numpy as np
from scipy.stats import genpareto, genextreme
import pandas as pd
from scipy.stats import zipf
from tqdm import tqdm

# 设定参数
num_files = 10000  # 生成文件的数量
num_keys_per_file = 1000  # 每个文件中key的数量
num_keys = 100000  # key的数量
key_range = (1, num_keys)  # key的范围
operations = ['GET', 'PUT', 'DELETE']  # 操作类型
a = 1.5  # 形状参数可以根据需要调整，以匹配特定的分布特性
op_probabilities = [0.8, 0.15, 0.05]  # 操作的概率，您可以根据需要进行调整

for file_index in tqdm(range(1, num_files + 1)):
    # Key大小分布的GEV参数
    gev_params_key = {'c': 0.078688, 'loc': 30.7984, 'scale': 8.20449}

    # Value大小分布的GP参数
    gp_params_value = {'c': 0.348238, 'loc': 0, 'scale': 214.476}

    # 生成keys
    keys = zipf.rvs(a, size = num_keys)

    # 生成每个key的size（基于GEV分布）
    key_sizes = np.floor(genextreme.rvs(c=gev_params_key['c'], loc=gev_params_key['loc'], scale=gev_params_key['scale'], size=num_keys)).astype(int)


    # 生成每个value的size（基于GP分布）
    value_sizes = np.floor(genpareto.rvs(c=gp_params_value['c'], loc=gp_params_value['loc'], scale=gp_params_value['scale'], size=num_keys)).astype(int)


    # 生成随机字符串values，每个字符串长度由value_sizes决定
    values = [''.join(np.random.choice(list('abcdefghijklmnopqrstuvwxyz'), int(size))) for size in value_sizes]

    # 生成操作类型
    operations_col = np.random.choice(operations, size=num_keys, p=op_probabilities)

    # 组合成DataFrame
    data = pd.DataFrame({
        'Key': keys,
        'key_length': key_sizes,
        'Value': values,
        'val_length': value_sizes,
        'Operation': operations_col
    })

    # 保存到CSV文件，如果您需要不同的格式或者直接输出到屏幕，请调整这部分代码
    data.to_csv(f'etc_data{file_index}.csv', index=False)

# 打印前5条数据以检查
print(data.head())
