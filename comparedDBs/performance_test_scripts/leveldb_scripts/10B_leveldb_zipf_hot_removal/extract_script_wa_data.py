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
    wa_data_re = re.compile(r'user_io:(\d+\.?\d*)MB total_ios: (\d+\.?\d*)MB WriteAmplification: (\d+\.?\d*)')

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
            found_level = True
            match = wa_data_re.match(line)
            print(f"Checking potential percent line i: {i}. data: {match.groups()}")  # Debug output
            i = i + 1
            results.append((record_point_index, match.groups()))
            # print(f"Found matching percent line at {j} for record point {record_point_index}")  # Debug output

            if not found_level:
                # If no matching Percent line was found, add default values
                results.append((record_point_index, ('0.0', '0.0', '0.0')))
                # print(f"No matching percent line found for level at line {i} record_point_index is:{record_point_index}, default values added")  # Debug output
            continue
        i = i + 1
    
    if results and results[0][0] > 1:
        for k in range(1, results[0][0]):
            results.insert(k - 1, (k, ('0.0', '0.0', '0.0')))
    return results





def process_data_and_save_to_file(extracted_data, output_file_path):
    # 初始化一个空的二维列表（类似于C++中的vector<vector<float>>）
    ratios_list = []

    for index, data in extracted_data:
        # 将字符串数据转换为整数
        data_int = [float(x) for x in data[:3]]
        debug_print("Converted data to integers:", data_int)  # 输出转换后的整数数组
        # 计算第二个和第三个数据的和
        user_io = data_int[0] 
        debug_print("Sum of second and third elements (total_r):", user_io) 
        # 计算第四个和第五个数据的和
        total_io = data_int[1]
        debug_print("Sum of fourth and fifth elements (total_w):", total_io) 
        wa = data_int[2]
        debug_print("Sum of fourth and fifth elements (wa):", wa)
        
        # exit(0)

        # 将这四个比例加入到列表中
        ratios_list.append([user_io, total_io, wa])

    # 将结果写入文本文件
    with open(output_file_path, 'w') as file:
        for record_index, ratios in enumerate(ratios_list, start=0):
            file.write(f"{record_index+1} {ratios[0]} {ratios[1]} {ratios[2]}\n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hot_removal/leveldb_10B_val_128_mem1MB_zipf1.3.log'  # 替换为您的日志文件路径

extracted_data = extract_percent_data(log_file_path)
for index, data in extracted_data:
    if index < 10:
        print(f"Record Point {index}: {' '.join(data)}")
print(f"Extracted data: {len(extracted_data)} records\n\n")
# 使用示例
output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hot_removal/leveldb_wa_outputfile_mem1MB_1.3.txt'  # 替换为您想保存结果的文件路径
process_data_and_save_to_file(extracted_data, output_file_path)

