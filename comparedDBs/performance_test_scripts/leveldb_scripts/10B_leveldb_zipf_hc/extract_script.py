# import re

# def extract_level_data(log_file_path, level):
#     # 初始化结果列表
#     results = []

#     # 用于匹配记录点开始的正则表达式，确保是50个'-'
#     record_point_start_re = re.compile(r'^-{50}$')
#     # 用于匹配空白行的正则表达式
#     blank_line_re = re.compile(r'^\s*$')
#     # 用于匹配level数据行的正则表达式
#     level_data_re = re.compile(rf'^\s*{level}\s+')
#     # 用于匹配Percent行的正则表达式
#     percent_re = re.compile(r'^Percent:')

#     # 开始读取文件
#     with open(log_file_path, 'r') as file:
#         record_point_found = False
#         capture_data = False
#         for line in file:
#             # 检测记录点开始
#             if record_point_start_re.match(line):
#                 # 如果之前的记录点中有开始捕获但未结束的level数据，则结束它并默认Percent数据为0
#                 if capture_data:
#                     results.append(data + ' Percent: 0')
#                     capture_data = False
#                 record_point_found = True
#                 continue

#             # 如果找到记录点但还未找到对应level的数据，则预设该level数据为0
#             if record_point_found and not capture_data:
#                 data = f"Level {level} data missing at this record point. Default Percent: 0"
#                 results.append(data)
#                 record_point_found = False  # 重置记录点发现状态

#             # 跳过空白行
#             if blank_line_re.match(line):
#                 continue

#             # 如果是指定level的数据行，开始捕获数据
#             if level_data_re.match(line):
#                 capture_data = True
#                 data = line.strip()
#                 continue  # 继续读取下一行以查找Percent行

#             # 如果已经开始捕获数据，并且是Percent行
#             if capture_data and percent_re.match(line):
#                 # 将Percent行数据追加到当前level数据后
#                 data += ' ' + line.strip()
#                 results.append(data)
#                 capture_data = False  # 停止捕获数据
#                 continue

#             # 如果已经开始捕获数据，但下一行不是Percent行
#             if capture_data and not percent_re.match(line):
#                 # 默认Percent数据为0，并停止捕获
#                 results.append(data + ' Percent: 0')
#                 capture_data = False

#     return results

# # 使用示例
# log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/test.log'  # 替换为您的日志文件路径
# level = 1  # 替换为您想提取的level编号
# extracted_data = extract_level_data(log_file_path, level)
# for data in extracted_data:
#     print(data)






import re

def extract_percent_data(log_file_path, level):
    # 初始化结果列表
    results = []

    # 用于匹配记录点开始的正则表达式，确保是50个'-'
    record_point_start_re = re.compile(r'^-{50}$')
    # 用于匹配level数据行的正则表达式
    level_data_re = re.compile(rf'^\s*{level}\s+')
    # 用于匹配Percent行及其数据的正则表达式
    percent_re = re.compile(r'^Percent:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)')

    record_point_index = 0  # 用于标记记录点的索引
    with open(log_file_path, 'r') as file:
        for line in file:
            # 检测记录点开始
            if record_point_start_re.match(line):
                if record_point_index > 0:  # 确保不是文件的第一个记录点
                    # 对于上一个记录点，如果没有添加数据（即没有对应level的数据），则添加默认值
                    while len(results) < record_point_index:
                        results.append((record_point_index, ('0', '0', '0', '0', '0')))
                record_point_index += 1  # 更新记录点索引

            # 如果是指定level的数据行
            if level_data_re.match(line):
                next_line = next(file, '')  # 读取下一行
                match = percent_re.match(next_line)
                if match:
                    # 如果找到Percent行，提取五个数字
                    results.append((record_point_index, match.groups()))
                else:
                    # 如果下一行不是Percent行，确保此时不重复添加默认值
                    if len(results) < record_point_index or results[-1][0] != record_point_index:
                        results.append((record_point_index, ('0', '0', '0', '0', '0')))

    # 确保文件末尾的最后一个记录点也被处理
    if len(results) < record_point_index:
        results.append((record_point_index, ('0', '0', '0', '0', '0')))
    
    return results


def process_data_and_save_to_file(extracted_data, output_file_path):
    # 初始化一个空的二维列表（类似于C++中的vector<vector<float>>）
    ratios_list = []

    for index, data in extracted_data:
        # 将字符串数据转换为整数
        data_int = [int(x) for x in data]
        # 计算第二个和第三个数据的和
        total_r = data_int[1] + data_int[2]
        # 计算第四个和第五个数据的和
        total_w = data_int[3] + data_int[4]

        if total_r > 0:  # 确保分母不为0
            # 计算比例
            ratio_1rd = data_int[1] / total_r
            ratio_2th = data_int[2] / total_r
        else:
            ratio_1rd, ratio_2th = 0, 0  # 如果和为0，则比例默认为0

        if total_w > 0:  # 确保分母不为0
            # 计算比例
            ratio_3rd = data_int[3] / total_w
            ratio_4th = data_int[4] / total_w
        else:
            ratio_3rd, ratio_4th = 0, 0  # 如果和为0，则比例默认为0

        # 将这四个比例加入到列表中
        ratios_list.append([ratio_1rd, ratio_2th, ratio_3rd, ratio_4th])

    # 将结果写入文本文件
    with open(output_file_path, 'w') as file:
        for record_index, ratios in enumerate(ratios_list, start=0):
            file.write(f"{record_index+1} {ratios[0]} {ratios[1]} {ratios[2]} {ratios[3]}\n")

# 使用示例
log_file_path = '/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/test.log'  # 替换为您的日志文件路径

levels=[1,2,3,4,5]

for level in levels:
    extracted_data = extract_percent_data(log_file_path, level)
    for index, data in extracted_data:
        if index == 1:
            print(f"Record Point {index}: {' '.join(data)}")
        # print(f"Record Point {index}: {' '.join(data)}")
    # 使用示例
    output_file_path = f'/home/jeff-wang/WorkloadAnalysis/comparedDBs/performance_test_scripts/leveldb_scripts/10B_leveldb_zipf_hc/extract_floder/outputfile{level}.txt'  # 替换为您想保存结果的文件路径
    process_data_and_save_to_file(extracted_data, output_file_path)

