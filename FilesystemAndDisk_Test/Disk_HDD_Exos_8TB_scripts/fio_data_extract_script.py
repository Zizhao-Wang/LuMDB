import re
import os

def convert_bandwidth(value, unit):
    """
    将MiB/s, GiB/s转换为KiB/s, MB/s, GB/s转换为KB/s
    """
    value = float(value)
    if unit == "MiB/s":
        return value * 1024  # 1 MiB/s = 1024 KiB/s
    elif unit == "GiB/s":
        return value * 1024 * 1024  # 1 GiB/s = 1048576 KiB/s
    elif unit == "MB/s":
        return value * 1000  # 1 MB/s = 1000 KB/s
    elif unit == "GB/s":
        return value * 1000 * 1000  # 1 GB/s = 1000000 KB/s
    elif unit == "KiB/s":
        return value
    elif unit == "kB/s":
        return value
    return value

def extract_data_from_file(file_path, pattern):
    with open(file_path, 'r') as file:
        content = file.read()
        match = re.search(pattern, content)
        if match:
            groups = match.groups()
            # 转换IOPS
            iops = groups[0]
            if 'k' in groups[1]:
                iops = str(float(iops) * 1000)  # 将k转换为千
            # 转换带宽
            bw_kib = convert_bandwidth(groups[2], groups[3])
            bw_kb = convert_bandwidth(groups[4], groups[5])
            # 保留GiB
            size_gib = groups[6]
            # 保留时长
            duration = groups[8]
            print(f"Match found in {file_path}: {iops}, {bw_kib}, {bw_kb}, {size_gib}, {duration}")
            return (iops, bw_kib, bw_kb, size_gib, duration)
        else:
            print(f"No match found in {file_path}")
            return None

def process_directory(directory_path, pattern):
    """
    处理指定目录下的所有文件,并将结果保存到txt文件中
    """
    results_rand = []
    results_seq = []
    action = 'read' if 'read' in directory_path else 'write'
    # 遍历随机和顺序读或写
    for mode in ['rand', 'seq']:
        for i in range(1, 33):
            file_name = f"{mode}_{action}_test_{i}_jobs.out"
            file_path = os.path.join(directory_path, file_name)
            data = extract_data_from_file(file_path, pattern)
            if data:
                if mode == 'rand':
                    results_rand.append(data)
                else:
                    results_seq.append(data)
    
    results_combined = [(i,) + rand + seq for i, (rand, seq) in enumerate(zip(results_rand, results_seq), start=1)]
    

    output_directory = os.path.join(os.path.dirname(directory_path), 'data_outputs')
    if not os.path.exists(output_directory):
        os.makedirs(output_directory)  # 如果目录不存在则创建目录

    # 输出文件名
    output_file = os.path.join(output_directory, f'{action}_summary_4k_32threads_results.txt')
    with open(output_file, 'w') as out_file:
        for result in results_combined:
            line = '\t'.join(map(str, result)) + '\n'
            out_file.write(line)
    print(f"Data written to {output_file}")

# 定义需要处理的目录和它们对应的pattern
directories = {
    'exo_hdd_fio_test_read_ranseq': r'read: IOPS=(\d+\.?\d*)(k?), BW=(\d+\.?\d*)(KiB/s|MiB/s) \((\d+\.?\d*)(kB/s|MB/s)\)\((\d+\.?\d*)(MiB|GiB|KiB)/(\d+)msec\)',
    'exo_hdd_fio_test_write_ranseq': r'write: IOPS=(\d+\.?\d*)(k?), BW=(\d+\.?\d*)(KiB/s|MiB/s) \((\d+\.?\d*)(kB/s|MB/s)\)\((\d+\.?\d*)(MiB|GiB|KiB)/(\d+)msec\)'
}


# 获取当前脚本的目录路径
current_dir = os.path.dirname(os.path.realpath(__file__))


for directory, pattern in directories.items():
    dir_path = os.path.join(current_dir, directory)
    if os.path.exists(dir_path) and os.path.isdir(dir_path):
        process_directory(dir_path, pattern)
    else:
        print(f"Directory {dir_path} does not exist or is not a directory")

