import pandas as pd

def process_csv(input_file, m, output_ops_file):
    # 初始化读取操作列表
    read_ops = []
    
    # 逐行读取 CSV 文件
    with open(input_file, 'r') as f:
        # 跳过头部（假设有表头）
        header = f.readline()
        
        # 初始化计数器
        count = 0
        
        for line in f:
            # 按行分割并解析数据
            parts = line.strip().split(',')
            key = int(parts[0])  # 假设 'Key' 是文件的第一列
            
            # 只选择 Key >= 2000000 的数据
            if key >= 2000000:
                read_ops.append({'Operation': 'READ', 'Key': key})
                count += 1
            
            # 如果已经选择了 m 个读取操作，停止读取
            if count >= m:
                break
    
    # 将读取操作保存到 DataFrame
    read_ops_df = pd.DataFrame(read_ops)
    
    # 保存到 CSV 文件
    read_ops_df.to_csv(output_ops_file, index=False)
    print(f"操作数据已保存到 {output_ops_file}")

# 参数设置
input_file_template = '/home/jeff-wang/workloads/zipf{zipf_factor}_keys{keys}.csv'  # 输入文件路径模板
output_ops_file_template = '/home/jeff-wang/workloads/random_read_workload{zipf_factor}.csv'  # 输出文件路径模板

zipf_factor = 1.4  # 你可以改变这个值
keys = '10.0B'  # 你可以改变这个值
m = 500000  # 从去重后的数据中随机选取 m 个读取操作

# 根据模板生成实际的输入和输出文件路径
input_file = input_file_template.format(zipf_factor=zipf_factor, keys=keys)
output_ops_file = output_ops_file_template.format(zipf_factor=zipf_factor)

# 执行处理
process_csv(input_file, m, output_ops_file)
