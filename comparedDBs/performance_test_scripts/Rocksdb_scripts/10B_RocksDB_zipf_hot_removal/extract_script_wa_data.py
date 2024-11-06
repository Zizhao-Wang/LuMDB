import re

# 全局变量来控制调试输出
DEBUG = True

def debug_print(*args):
    """Print debug information only if debugging is enabled."""
    if DEBUG:
        print(*args)

def extract_total_ios(log_file_path):
    total_ios_re = re.compile(r'Cumulative compaction: ([\d.]+) GB write')
    total_ios = []

    with open(log_file_path, 'r') as file:
        lines = file.readlines()

    for i, line in enumerate(lines):
        match = total_ios_re.search(line)
        if match:
            total_ios_value = float(match.group(1))
            debug_print(f"Found total_ios at line {i}: {total_ios_value} GB")
            total_ios.append(total_ios_value)

    return total_ios

def extract_user_ios(log_file_path):
    user_ios_re = re.compile(r'Cumulative writes:.*ingest: ([\d.]+) GB')
    user_ios = []

    with open(log_file_path, 'r') as file:
        lines = file.readlines()

    for i, line in enumerate(lines):
        match = user_ios_re.search(line)
        if match:
            user_ios_value = float(match.group(1))
            debug_print(f"Found user_ios at line {i}: {user_ios_value} GB")
            user_ios.append(user_ios_value)

    return user_ios

def process_data_and_save_to_file(total_ios, user_ios, output_file_path):
    if len(total_ios) != len(user_ios):
        raise ValueError("Mismatch in the number of records for total_ios and user_ios")

    with open(output_file_path, 'w') as file:
        for record_index, (total_io, user_io) in enumerate(zip(total_ios, user_ios), start=1):
            file.write(f"{record_index}\t{total_io}\t{user_io}\n")
            debug_print(f"Writing record {record_index}: Total IO = {total_io} GB, User IO = {user_io} GB")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_10B_val128_mem8MB_zipf1.1.log'  # 替换为您的日志文件路径

total_ios = extract_total_ios(log_file_path)
user_ios = extract_user_ios(log_file_path)

debug_print(f"Extracted Total IOs: {total_ios}")
debug_print(f"Extracted User IOs: {user_ios}")

output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_removal/RocksDB_mem8MiB_1.1waoutput.txt'  # 替换为您想保存结果的文件路径
process_data_and_save_to_file(total_ios, user_ios, output_file_path)
