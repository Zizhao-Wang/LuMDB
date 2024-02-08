import os
import re
from decimal import Decimal, getcontext

# 设置Decimal精度，以确保在累加时保持准确
getcontext().prec = 3

from decimal import Decimal
import re

def extract_level_data(log_filename):
    # 初始化存储每个Level数据的字典列表
    level_data = {
        'Level 0': [], 'Level 1': [], 'Level 2': [], 'Level 3': [],
        'Level 4': [], 'Level 5': [],   # 根据需要调整Level的数量
    }

    # 定义每个Level的理论最大值（MB），假设Level 1为10MB，之后每个Level增加10倍
    theoretical_max_size = {
        'Level 0': 10,  # 这个值根据实际情况调整
        'Level 1': 10,
    }
    for i in range(2, len(level_data)):  # 从Level 2开始计算理论最大值
        theoretical_max_size['Level ' + str(i)] = theoretical_max_size['Level ' + str(i - 1)] * 10

    # 定义记录点的初始值和增量
    initial_record_point = Decimal('0.0')
    record_point_increment = Decimal('1.0')
    # 定义最大记录点值，这个值应根据您日志文件中的最大记录点来设置
    max_record_point = Decimal('9.0')
    max_record_point2 = Decimal('10.0')

    # 为每个Level的每个记录点初始化数据
    record_point = initial_record_point
    while record_point <= max_record_point:
        for level in level_data.keys():
            level_data[level].append({
                'Files': 0,
                'Size(MB)': 0,
                'Time(sec)': 0,
                'Read(MB)': 0,
                'Write(MB)': 0,
                'Keys Written': float(record_point),  # 使用Decimal确保精度，最后转换为float方便理解
                'Theoretical Max Size(MB)': theoretical_max_size[level],  # 添加每个level的理论最大值
            })
        record_point += record_point_increment

    # 打开并逐行读取日志文件
    with open(log_filename, 'r') as file:
        current_record_point = initial_record_point
        in_data_section = False
        # 添加计数器来跟踪已处理的记录点数量
        processed_record_points = Decimal('0.0')

        for line in file:
            if "--------------------------------------------------" in line:
                # 在增加新的记录点前检查是否已达到max_record_point的限制
                if processed_record_points >= max_record_point2:
                    # 如果已处理的记录点数量达到预设的最大值，则停止处理新的记录点
                    # print(f"test:{processed_record_points}")
                    break
                in_data_section = True
                continue
            if line.startswith("user_io:"):
                in_data_section = False
                current_record_point += record_point_increment
                # 更新已处理的记录点计数器
                processed_record_points += record_point_increment
                continue
            if in_data_section:
                match = re.match(r'\s*(\d)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', line)
                if match:
                    level = 'Level ' + match.group(1)
                    index = int((current_record_point - initial_record_point) / record_point_increment)                   
                    # 更新当前记录点的数据
                    level_data[level][index] = {
                        'Files': int(match.group(2)),
                        'Size(MB)': int(match.group(3)),
                        'Time(sec)': int(match.group(4)),
                        'Read(MB)': int(match.group(5)),
                        'Write(MB)': int(match.group(6)),
                        'Keys Written': float(current_record_point),
                        'Theoretical Max Size(MB)': theoretical_max_size[level],
                    }
    return level_data

def process_log_files(log_dir, output_dir):
    # 确保输出目录存在
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"创建输出目录：{output_dir}")

    # 遍历给定目录及其所有子目录下的所有文件
    for root, dirs, files in os.walk(log_dir):
        for filename in files:
            if filename.endswith(".log"):  # 确保只处理.log文件
                log_path = os.path.join(root, filename)
                print(f"正在处理文件：{log_path}")
                level_data = extract_level_data(log_path)

                # 确保每个子目录的输出文件存储在相应的输出子目录中
                relative_path = os.path.relpath(root, log_dir)
                specific_output_dir = os.path.join(output_dir, relative_path)
                if not os.path.exists(specific_output_dir):
                    os.makedirs(specific_output_dir)
                    print(f"创建子目录输出目录：{specific_output_dir}")

                # 创建输出文件名，替换.log为.out
                output_filename = filename.replace(".log", ".out")
                output_path = os.path.join(specific_output_dir, output_filename)

                # 写入提取的数据到输出文件
                with open(output_path, 'w') as out_file:
                    # 写入 #Datamovement 数据
                    for level, records in level_data.items():
                        if level == 'Level 0':
                            continue  # 如果当前是 Level 0，则跳过当前迭代，不执行后面的代码
                        out_file.write(f"#Datamovement {level}:\n")
                        for record in records:
                            out_file.write(f"{record['Keys Written']}\t{record['Write(MB)']}\n")
                        out_file.write("e\n\n")

                    # 写入 #Write amplification 数据
                    for level, records in level_data.items():
                        if level == 'Level 0':
                            continue  # 如果当前是 Level 0，则跳过当前迭代，不执行后面的代码
                        out_file.write(f"#Write amplification{level}:\n")
                        for record in records:
                            out_file.write(f"{record['Keys Written']}\t{record['Theoretical Max Size(MB)']}\t{record['Theoretical Max Size(MB)']+record['Write(MB)']}\n")
                        out_file.write("e\n\n")

                print(f"数据已写入：{output_path}")

# 用于示例的日志文件目录和输出目录
log_dir = '/home/wangzizhao/WorkloadAnalysis/scripts/leveldb_etc'  # 确保这是您的日志文件目录
output_dir = '/home/wangzizhao/WorkloadAnalysis/data_extraction/etc_extraction'  # 确保这是您的输出目录

# 调用函数处理日志文件并输出结果
process_log_files(log_dir, output_dir)
