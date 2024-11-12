import re

# 全局变量来控制调试输出
DEBUG = False

def debug_print(*args):
    """Print debug information only if debugging is enabled."""
    if DEBUG:
        print(*args)


def extract_percent_data(log_file_path):
    results = []
    record_point_start_re = re.compile(r'^-{50}$')
    wa_data_re = re.compile(r'WriteAmplification: (\d+\.?\d*)')

    with open(log_file_path, 'r') as file:
        lines = file.readlines()

    record_point_index = 0
    i = 0
    while i < len(lines):
        line = lines[i]
        if record_point_start_re.match(line):
            record_point_index = record_point_index+1
            # print(f"New record point {record_point_index} at line {i}")  # Debug output
            i=i+1
            continue

        if wa_data_re.match(line):
            print(f"Checking potential percent line i: {i}. data: {line}")  # Debug output
            match = wa_data_re.match(line)
            i = i + 1
            print(f"Matched line {i}: {line.strip()} -> {match.group(1)}")
            results.append((record_point_index, match.group(1)))
            # print(f"Found matching percent line at {j} for record point {record_point_index}")  # Debug output
        
        i = i + 1
    
    if results and results[0][0] > 1:
        for k in range(1, results[0][0]):
            results.insert(k - 1, (k, ( '0.0')))
    return results





def process_data_and_save_to_file(extracted_data, output_file_path):
    wa_list = []

    for index, wa in extracted_data:
        print(f"Processing index {index}: wa = {wa}")  # Debug output
        wa_float = float(wa)
        wa_list.append(wa_float)

    with open(output_file_path, 'w') as file:
        for record_index, wa in enumerate(wa_list, start=0):
            file.write(f"{record_index + 1} {wa}\n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/pebblesdb_scripts/10B_Pebblesdb_zipf_hot_removal/Pebbles10B_10B_val_128_mem64MB_zipf1.1.log'  # 替换为您的日志文件路径

extracted_data = extract_percent_data(log_file_path)
for index, data in extracted_data:
    if index < 10:
        print(f"Record Point {index}: {' '.join(data)}")
print(f"Extracted data: {len(extracted_data)} records\n\n")

# 使用示例
output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/pebblesdb_scripts/10B_Pebblesdb_zipf_hot_removal/Pebblesdb_mem64MiB_1.1waoutput.txt'  # 替换为您想保存结果的文件路径
process_data_and_save_to_file(extracted_data, output_file_path)

