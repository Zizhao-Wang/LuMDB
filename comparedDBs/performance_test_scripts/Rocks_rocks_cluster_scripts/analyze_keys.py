#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import glob
from collections import Counter
import math

def analyze_single_cluster_file(file_path, max_lines, output_dir):
    """
    分析单个cluster文件中key的分布情况
    
    Args:
        file_path: 单个文件的完整路径
        max_lines: 要处理的最大行数
        output_dir: 输出文件的目录
    """
    if not os.path.exists(file_path):
        print(f"错误: 文件 {file_path} 不存在")
        return
    
    # 从文件名提取cluster编号
    filename = os.path.basename(file_path)
    cluster_name = filename.replace('.sort', '')
    
    print(f"正在分析文件: {file_path}")
    print(f"最大处理行数: {max_lines:,}")
    
    # 存储key的计数
    key_counter = Counter()
    lines_processed = 0
    
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                if lines_processed >= max_lines:
                    break
                
                line = line.strip()
                if line:
                    parts = line.split(',')
                    if len(parts) >= 2:
                        key = parts[1]  # 第二列是key
                        key_counter[key] += 1
                        lines_processed += 1
                        
                        # 每5000万行显示进度
                        if lines_processed % 50_000_000 == 0:
                            print(f"  已处理 {lines_processed:,} 行")
        
        print(f"  文件处理完成，共处理 {lines_processed:,} 行")
        
    except Exception as e:
        print(f"处理文件时出错: {e}")
        return
    
    print(f"发现 {len(key_counter):,} 个唯一的key")
    
    # 任务1: 计算unique key的百分位数
    unique_key_count = len(key_counter)
    percentiles = [1, 5, 10, 15, 20, 30, 35, 40, 45, 50]
    
    # 任务2: 分析高频key的占比
    most_common_keys = key_counter.most_common()
    top_n_list = [10, 100, 500, 1000, 2000, 5000, 10000, 20000,35465, 50000,70000, 100000, 462547, 500000,600000]
    
    # 创建输出文件
    output_filename = f"{cluster_name}_analysis_results.txt"
    output_path = os.path.join(output_dir, output_filename)
    
    # 显示结果到控制台
    print("\n" + "="*60)
    print(f"文件 {filename} 分析结果")
    print("="*60)
    
    print(f"\n总处理行数: {lines_processed:,}")
    print(f"唯一key总数: {unique_key_count:,}")
    
    print("\n任务1: Unique Key 百分位数分析")
    print("-" * 40)
    for p in percentiles:
        key_count = math.ceil(unique_key_count * p / 100)
        print(f"前 {p:2d}%: {key_count:,} 个key")
    
    print("\n任务2: 高频Key占比分析")
    print("-" * 40)
    print("排名范围        总出现次数    占总数据比例")
    print("-" * 45)
    
    for n in top_n_list:
        if n <= len(most_common_keys):
            top_n_total = sum(count for _, count in most_common_keys[:n])
            percentage = (top_n_total / lines_processed) * 100
            print(f"前 {n:6,} 个key: {top_n_total:12,} 次  {percentage:6.2f}%")
        else:
            print(f"前 {n:6,} 个key: 数据不足（总共只有 {len(most_common_keys):,} 个unique key）")
    
    print("\n出现次数最多的前10个Key详情:")
    print("-" * 40)
    print("排名    Key          出现次数    占比")
    print("-" * 40)
    
    for i, (key, count) in enumerate(most_common_keys[:10], 1):
        percentage = (count / lines_processed) * 100
        print(f"{i:2d}    {key:12s} {count:10,}  {percentage:6.2f}%")
    
    # 保存结果到文件
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f"文件分析结果: {filename}\n")
        f.write(f"分析时间: {__import__('datetime').datetime.now()}\n")
        f.write(f"文件路径: {file_path}\n")
        f.write(f"处理行数: {lines_processed:,}\n")
        f.write(f"唯一key数量: {unique_key_count:,}\n")
        f.write("="*60 + "\n\n")
        
        f.write("任务1: Unique Key 百分位数分析\n")
        f.write("-" * 40 + "\n")
        for p in percentiles:
            key_count = math.ceil(unique_key_count * p / 100)
            f.write(f"前 {p:2d}%: {key_count:,} 个key\n")
        
        f.write("\n任务2: 高频Key占比分析\n")
        f.write("-" * 40 + "\n")
        f.write("排名范围        总出现次数    占总数据比例\n")
        f.write("-" * 45 + "\n")
        
        for n in top_n_list:
            if n <= len(most_common_keys):
                top_n_total = sum(count for _, count in most_common_keys[:n])
                percentage = (top_n_total / lines_processed) * 100
                f.write(f"前 {n:6,} 个key: {top_n_total:12,} 次  {percentage:6.2f}%\n")
            else:
                f.write(f"前 {n:6,} 个key: 数据不足（总共只有 {len(most_common_keys):,} 个unique key）\n")
        
        f.write(f"\n出现次数最多的前10个Key详情:\n")
        f.write("-" * 40 + "\n")
        f.write("排名    Key          出现次数    占比\n")
        f.write("-" * 40 + "\n")
        
        for i, (key, count) in enumerate(most_common_keys[:10], 1):
            percentage = (count / lines_processed) * 100
            f.write(f"{i:2d}    {key:12s} {count:10,}  {percentage:6.2f}%\n")
    
    print(f"\n结果已保存到: {output_path}")
    return output_path

def analyze_cluster_files(directory_path, cluster_numbers, max_lines_per_file, output_dir):
    """
    分析多个cluster文件，每个文件单独分析
    
    Args:
        directory_path: 包含数据文件的目录路径
        cluster_numbers: 要处理的cluster文件编号列表
        max_lines_per_file: 每个文件要处理的最大行数
        output_dir: 输出结果文件的目录
    """
    # 确保输出目录存在
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"创建输出目录: {output_dir}")
    
    print(f"开始分析 {len(cluster_numbers)} 个cluster文件...")
    print(f"数据目录: {directory_path}")
    print(f"输出目录: {output_dir}")
    print(f"每个文件处理行数: {max_lines_per_file:,}")
    print("="*60)
    
    results = []
    
    for i, num in enumerate(cluster_numbers, 1):
        print(f"\n[{i}/{len(cluster_numbers)}] 处理 cluster{num}")
        print("-" * 30)
        
        file_path = os.path.join(directory_path, f"second_cluster{num}.sort")
        
        if os.path.exists(file_path):
            result_file = analyze_single_cluster_file(file_path, max_lines_per_file, output_dir)
            if result_file:
                results.append(result_file)
        else:
            print(f"警告: 文件 second_cluster{num}.sort 不存在，跳过")
    
    print("\n" + "="*60)
    print("所有文件分析完成!")
    print(f"成功分析了 {len(results)} 个文件")
    print("生成的结果文件:")
    for result_file in results:
        print(f"  - {result_file}")

if __name__ == "__main__":
    # ============ 配置参数 ============
    # 数据文件目录路径
    directory_path = "/mnt/nvm"
    
    # 要分析的cluster文件编号
    cluster_numbers = [49,35,40,30,25,51,1,13]
    
    # 每个文件要处理的行数
    max_lines_per_file = 500_000_000  # 5亿行
    
    # 输出结果文件的目录
    output_dir = "/home/jeff-wang/LuMDB/comparedDBs/performance_test_scripts/Rocks_rocks_cluster_scripts/cluster_data_analysis"  # 请根据需要修改这个路径
    
    # ================================
    
    print("Cluster文件Key分析工具")
    print("="*60)
    
    analyze_cluster_files(directory_path, cluster_numbers, max_lines_per_file, output_dir)
    
    print("\n分析完成!")