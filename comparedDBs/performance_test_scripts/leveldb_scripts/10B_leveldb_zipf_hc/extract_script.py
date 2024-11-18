# def extract_percent_data(log_file_path, level):
#     # 初始化结果列表
#     results = []

#     # 用于匹配记录点开始的正则表达式，确保是50个'-'
#     record_point_start_re = re.compile(r'^-{50}$')
#     # 用于匹配level数据行的正则表达式
#     level_data_re = re.compile(rf'^\s*{level}\s+')
#     # 用于匹配Percent行及其数据的正则表达式
#     percent_re = re.compile(r'^Percent:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)')

#     record_point_index = 0  # 用于标记记录点的索引
#     with open(log_file_path, 'r') as file:
#         for line in file:
#             # 检测记录点开始
#             if record_point_start_re.match(line):
#                 if record_point_index > 0:  # 确保不是文件的第一个记录点
#                     # 对于上一个记录点，如果没有添加数据（即没有对应level的数据），则添加默认值
#                     while len(results) < record_point_index:
#                         results.append((record_point_index, ('0', '0', '0', '0', '0')))
#                 record_point_index += 1  # 更新记录点索引

#             # 如果是指定level的数据行
#             if level_data_re.match(line):
#                 next_line = next(file, '')  # 读取下一行
#                 match = percent_re.match(next_line)
#                 if match:
#                     # 如果找到Percent行，提取五个数字
#                     results.append((record_point_index, match.groups()))
#                 else:
#                     # 如果下一行不是Percent行，确保此时不重复添加默认值
#                     if len(results) < record_point_index or results[-1][0] != record_point_index:
#                         results.append((record_point_index, ('0', '0', '0', '0', '0')))

#     # 确保文件末尾的最后一个记录点也被处理
#     if len(results) < record_point_index:
#         results.append((record_point_index, ('0', '0', '0', '0', '0')))
    
#     return results




import re

# 全局变量来控制调试输出
DEBUG = False

def debug_print(*args):
    """Print debug information only if debugging is enabled."""
    if DEBUG:
        print(*args)


def extract_percent_data(log_file_path, level, percent, percent_number):
    results = []
    record_point_start_re = re.compile(r'^-{50}$')
    level_data_re = re.compile(rf'^\s*{level}\s+')
    percent_re = re.compile(rf'^Percent:\s+{percent_number}\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)')

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
            found_percent = False
            j = i + percent  # Start checking from the line right after the matched level line
            if(j < len(lines)):
                # print(f"Checking potential percent line {j} and i {i}: {percent_line}")  # Debug output
                percent_line = lines[j].strip()
                
                match = percent_re.match(percent_line)
                if match:
                    results.append((record_point_index, match.groups()))
                    found_percent = True
            else:
                print(f"Checking potential percent line j: {j} and i {i} out of range: {percent_line}")  # Debug output
                found_percent = False
                # print(f"Found matching percent line at {j} for record point {record_point_index}")  # Debug output

            if not found_percent:
                # If no matching Percent line was found, add default values
                results.append((record_point_index, ('0', '0', '0', '0', '0')))
                # print(f"No matching percent line found for level at line {i} record_point_index is:{record_point_index}, default values added")  # Debug output
            i = j
            continue
        i=i+1 
    
    if results and results[0][0] > 1:
        for k in range(1, results[0][0]):
            results.insert(k - 1, (k, ('0', '0', '0', '0', '0')))
    return results





def process_data_and_save_to_file(extracted_data, output_file_path):
    # 初始化一个空的二维列表（类似于C++中的vector<vector<float>>）
    ratios_list = []

    for index, data in extracted_data:
        # 将字符串数据转换为整数
        data_int = [int(x) for x in data]
        debug_print("Converted data to integers:", data_int)  # 输出转换后的整数数组
        # 计算第二个和第三个数据的和
        total_r = data_int[0] + data_int[1]
        debug_print("Sum of second and third elements (total_r):", total_r) 
        # 计算第四个和第五个数据的和
        total_w = data_int[2] + data_int[3]
        debug_print("Sum of fourth and fifth elements (total_w):", total_w) 

        if total_r > 0:  # 确保分母不为0
            # 计算比例
            ratio_1rd = data_int[0] / total_r
            ratio_2th = data_int[1] / total_r
            debug_print("Ratio for second data (ratio_1rd):", ratio_1rd)
            debug_print("Ratio for third data (ratio_2th):", ratio_2th)
        else:
            ratio_1rd, ratio_2th = 0, 0  # 如果和为0，则比例默认为0
            debug_print("Total_r is zero, set ratio_1rd and ratio_2th to 0")

        if total_w > 0:  # 确保分母不为0
            # 计算比例
            ratio_3rd = data_int[2] / total_w
            ratio_4th = data_int[3] / total_w
            debug_print("Ratio for fourth data (ratio_3rd):", ratio_3rd)
            debug_print("Ratio for fifth data (ratio_4th):", ratio_4th)
        else:
            ratio_3rd, ratio_4th = 0, 0  # 如果和为0，则比例默认为0
            debug_print("Total_w is zero, set ratio_3rd and ratio_4th to 0")
        
        # exit(0)

        # 将这四个比例加入到列表中
        ratios_list.append([ratio_1rd, ratio_2th, ratio_3rd, ratio_4th])

    # 将结果写入文本文件
    with open(output_file_path, 'w') as file:
        for record_index, ratios in enumerate(ratios_list, start=0):
            file.write(f"{record_index+1} {ratios[0]} {ratios[1]} {ratios[2]} {ratios[3]}\n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/test.log'  # 替换为您的日志文件路径

levels=[1,2,3,4,5]

percentss=[1,2,3,4,5,6,7]

dictionary = {
    1: 1,
    2: 5,
    3: 10,
    4: 15,
    5: 20,
    6: 25,
    7: 30
}

for percent_select in percentss:
    for level in levels:
        extracted_data = extract_percent_data(log_file_path, level, percent_select,dictionary[percent_select])
        for index, data in extracted_data:
            if index < 10:
                print(f"Record Point {index}: {' '.join(data)}")
        print(f"level {level} extracted data: {len(extracted_data)} records\n\n")
        # 使用示例
        output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/extract_floder/zipf1.1/percent{dictionary[percent_select]}/outputfile{level}.txt'  # 替换为您想保存结果的文件路径
        process_data_and_save_to_file(extracted_data, output_file_path)

