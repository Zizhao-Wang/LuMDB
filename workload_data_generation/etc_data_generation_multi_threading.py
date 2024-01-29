import numpy as np
import pandas as pd
from scipy.stats import genextreme, genpareto, zipf
from concurrent.futures import ThreadPoolExecutor

# GEV分布的参数，用于生成key的size
gev_params_key = {'c': 0.078688, 'loc': 30.7984, 'scale': 8.20449}

# GP分布的参数，用于生成value的size
gp_params_value = {'c': 0.348238, 'loc': 0, 'scale': 214.476}

def generate_keys(a, num_keys):
    return zipf.rvs(a, size=num_keys)

def generate_key_sizes(num_keys):
    return np.floor(genextreme.rvs(c=gev_params_key['c'], loc=gev_params_key['loc'], 
                                   scale=gev_params_key['scale'], size=num_keys)).astype(int)

def generate_value_sizes(num_keys):
    return np.floor(genpareto.rvs(c=gp_params_value['c'], loc=gp_params_value['loc'], 
                                  scale=gp_params_value['scale'], size=num_keys)).astype(int)

def generate_values(value_sizes):
    return [''.join(np.random.choice(list('abcdefghijklmnopqrstuvwxyz'), int(size))) for size in value_sizes]

def generate_operations(operations, op_probabilities, num_keys):
    return np.random.choice(operations, size=num_keys, p=op_probabilities)

def generate_batch_data(a, num_keys, operations, op_probabilities):

    with ThreadPoolExecutor(max_workers=4) as executor:
        keys_future = executor.submit(generate_keys, a, num_keys)
        key_sizes_future = executor.submit(generate_key_sizes, num_keys)
        value_sizes_future = executor.submit(generate_value_sizes,num_keys)   
        operations_col_future = executor.submit(generate_operations,operations, op_probabilities, num_keys)

        keys = keys_future.result()
        key_sizes = key_sizes_future.result()
        value_sizes = value_sizes_future.result()
        # values = generate_values(value_sizes)
        operations_col = operations_col_future.result()

    return pd.DataFrame({
        'Key': keys,
        'Key_Size': key_sizes,
        # 'Value': values,
        'Value_Size': value_sizes,
        'Operation': operations_col
    })

def generate_data(total_keys, a, operations, op_probabilities, batch_size):
    batches = total_keys // batch_size
    for batch in range(batches):
        data = generate_batch_data(a, batch_size, operations, op_probabilities)
        mode = 'w' if batch == 0 else 'a'
        header = True if batch == 0 else False
        data.to_csv(f'/home/wangzizhao/workloads/generated_data.csv', mode=mode, index=False, header=header)
        print(f'Batch {batch + 1}/{batches} for file written.')

# Example usage
if __name__ == "__main__":
    num_files = 1
    total_keys = 1000000000  # 1 billion keys to generate
    batch_size = 100000  # Number of keys per batch
    a = 1.5
    operations = ['GET', 'PUT', 'DELETE']
    op_probabilities = [0.7, 0.2, 0.1]
    
    # Adjust the number of workers based on your system's capabilities
    # with ThreadPoolExecutor(max_workers=4) as executor:
    #     for file_index in range(num_files):
    generate_data(total_keys, a, operations, op_probabilities, batch_size)