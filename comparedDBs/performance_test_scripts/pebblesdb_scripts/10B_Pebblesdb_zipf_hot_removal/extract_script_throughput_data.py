import re

# 全局变量来控制调试输出
DEBUG = True

def debug_print(*args):
    """Print debug information only if debugging is enabled."""
    if DEBUG:
        print(*args)

def extract_ops_per_second_data(log_file_path):
    results = []
    # 正则表达式匹配包含 ops/second 的行，并提取最后一个 ops/second 的值
    ops_re = re.compile(
        r'\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}.*thread 0:.*\(\d+,\d+\) ops and \(\d+\.\d+,(?P<ops_second>\d+\.\d+)\) ops/second'
    )

    with open(log_file_path, 'r') as file:
        for line in file:
            match = ops_re.search(line)
            if match:
                # 提取 ops/second 数据
                ops_per_second = match.group(1)
                results.append(ops_per_second)
                debug_print(f"Extracted ops/second: {ops_per_second}")

    return results

def process_data_and_save_to_file(extracted_data, output_file_path):
    with open(output_file_path, 'w') as file:
        for index, ops_per_second in enumerate(extracted_data, start=1):
            # 格式化输出为 "索引 ops_per_second"
            file.write(f"{index} {ops_per_second}\n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/pebblesdb_scripts/10B_Pebblesdb_zipf_hot_removal/Pebbles10B_10B_val_128_mem1MB_zipf1.5.log'  # 替换为您的日志文件路径

extracted_data = extract_ops_per_second_data(log_file_path)

# 使用示例
output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/pebblesdb_scripts/10B_Pebblesdb_zipf_hot_removal/Pebbles10B_1.5_throughput_output.txt'  # 替换为您想保存结果的文件路径
process_data_and_save_to_file(extracted_data, output_file_path)

