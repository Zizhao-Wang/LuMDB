import re

# 全局变量来控制调试输出
def debug_print(*args):
    """Print debug information only if debugging is enabled."""
    if DEBUG:
        print(*args)


def extract_percent_data(log_file_path, level):
    results = []
    record_point_start_re = re.compile(r'^-{50}$')
    level_data_re = re.compile(rf'^\s*{level}\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+\.\d+)\s+(\d+\.\d+)$')

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

        if level_data_re.match(line):
            # print(f"Checking potential percent line i: {i}. data: {line}")  # Debug output
            found_level = True
            match = level_data_re.match(line)
            # print(f"Checking potential percent line i: {i}. data: {match.groups()}")  # Debug output
            i = i + 1
            results.append((record_point_index, match.groups()))
            # print(f"Found matching percent line at {j} for record point {record_point_index}")  # Debug output

            if not found_level:
                # If no matching Percent line was found, add default values
                results.append((record_point_index, ('0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0.000', '0.000')))
                # print(f"No matching percent line found for level at line {i} record_point_index is:{record_point_index}, default values added")  # Debug output
            continue
        i = i + 1
    
    if results and results[0][0] > 1:
        for k in range(1, results[0][0]):
            results.insert(k - 1, (k, ('0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0.000', '0.000')))
    return results





def process_data_and_save_to_file(global_sums, level, output_file_path):
    # 初始化一个空的二维列表（类似于C++中的vector<vector<float>>）
    ratios_list = []
    extracted_data = all_data[level]

    for index, data in extracted_data:
        # 将字符串数据转换为整数
        data_int = [int(x) for x in data[:8]]
        debug_print("Converted data to integers:", data_int)  # 输出转换后的整数数组
        # 计算第二个和第三个数据的和
        extra_r = data_int[4] 
        debug_print("Sum of second and third elements (total_r):", extra_r) 
        # 计算第四个和第五个数据的和
        extra_w = data_int[6]
        debug_print("Sum of fourth and fifth elements (total_w):", extra_w) 

        # 将这四个比例加入到列表中
        ratios_list.append([extra_r, extra_w, global_sums[index-1][0], global_sums[index-1][1]])


    # 将结果写入文本文件
    with open(output_file_path, 'w') as file:
        for record_index, ratios in enumerate(ratios_list, start=0):
            file.write(f"{record_index+1} {ratios[0]} {ratios[1]} {ratios[2]} {ratios[3]} \n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/test.log'  # 替换为您的日志文件路径

levels=[1,2,3,4,5] #

DEBUG=False
all_data = {}

def calculate_sums(all_data):
    if not all_data:
        return []
    
    # 假设所有列表的长度都相同，使用第一个level的数据长度初始化global_sums
    first_level_data = next(iter(all_data.values()))
    global_sums = [(0, 0) for _ in first_level_data]

    for level, data in all_data.items():
        for index, record in enumerate(data):
            _, group = record
            extra_r = int(group[4])  # 第五个元素
            extra_w = int(group[6])  # 第七个元素
            # 累加当前记录点的第五和第七元素的和
            current_sum_r, current_sum_w = global_sums[index]
            global_sums[index] = (current_sum_r + extra_r, current_sum_w + extra_w)

    return global_sums


for level in levels:
    extracted_data = extract_percent_data(log_file_path, level)
    all_data[level]=extracted_data

global_sums = calculate_sums(all_data)



for level in levels:
    output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/extract_level_extra_data/outputfile{level}.txt'  # 替换为您想保存结果的文件路径
    process_data_and_save_to_file(global_sums, level, output_file_path)
