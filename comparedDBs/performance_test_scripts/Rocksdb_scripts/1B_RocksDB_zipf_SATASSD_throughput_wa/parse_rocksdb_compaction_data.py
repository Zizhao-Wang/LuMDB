import re
import csv
import os

def parse_rocksdb_log(log_file_path, output_csv_path):
    """
    解析RocksDB日志文件，提取throughput和WriteAmplification数据
    
    Args:
        log_file_path: 输入的日志文件路径
        output_csv_path: 输出的CSV文件路径
    """
    
    results = []
    entry_number = 1
    
    with open(log_file_path, 'r', encoding='utf-8') as file:
        content = file.read()
    
    # 匹配throughput行：2025/09/19-20:38:45  ... thread 0: (10000000,990000000) ops and (323656.9,335066.5) ops/second
    throughput_pattern = r'(\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2})\s+\.\.\.\s+thread 0:.*?ops and \([^,]+,([0-9.]+)\) ops/second'
    
    # 匹配 Cumulative compaction 行
    compaction_pattern = r'Cumulative compaction: ([0-9.]+) GB write'
    
    # 匹配 Cumulative writes 行中的 ingest 数据
    ingest_pattern = r'Cumulative writes:.*?ingest: ([0-9.]+) GB'
    
    # 找到所有throughput行
    throughput_matches = list(re.finditer(throughput_pattern, content))
    print(f"找到 {len(throughput_matches)} 个throughput匹配")
    
    # 如果没有找到匹配，进行调试
    if len(throughput_matches) == 0:
        print("调试：尝试寻找各个组件...")
        
        # 寻找时间戳行
        simple_timestamp_pattern = r'\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}\s+\.\.\.\s+thread 0:'
        simple_matches = re.findall(simple_timestamp_pattern, content)
        print(f"找到 {len(simple_matches)} 个时间戳匹配")
        
        # 寻找 ops/second 行
        ops_pattern = r'ops and \([^)]+\) ops/second'
        ops_matches = re.findall(ops_pattern, content)
        print(f"找到 {len(ops_matches)} 个 ops/second 匹配")
        
        # 寻找 Cumulative compaction 行
        comp_matches = re.findall(compaction_pattern, content)
        print(f"找到 {len(comp_matches)} 个 Cumulative compaction 匹配")
        
        # 寻找 Cumulative writes 行
        ingest_matches = re.findall(ingest_pattern, content)
        print(f"找到 {len(ingest_matches)} 个 ingest 匹配")
        
        # 显示前30行内容以便调试
        lines = content.split('\n')[:30]
        print("\n前30行内容:")
        for i, line in enumerate(lines):
            print(f"{i+1}: {line}")
        
        return results
    
    for match in throughput_matches:
        timestamp = match.group(1)
        throughput = float(match.group(2))  # 第二个数值（累积吞吐量）
        
        # 从当前匹配位置开始，向后查找Cumulative compaction和ingest数据
        start_pos = match.end()
        remaining_content = content[start_pos:]
        
        # 先找Cumulative compaction行
        compaction_match = re.search(compaction_pattern, remaining_content)
        if not compaction_match:
            print(f"警告：在 {timestamp} 后未找到 Cumulative compaction 数据")
            continue
            
        compaction_write_gb = float(compaction_match.group(1))
        
        # 从compaction匹配位置继续向后找ingest数据
        compaction_pos = compaction_match.end()
        remaining_content_2 = remaining_content[compaction_pos:]
        
        ingest_match = re.search(ingest_pattern, remaining_content_2)
        if not ingest_match:
            print(f"警告：在 {timestamp} 的 Cumulative compaction 后未找到 ingest 数据")
            continue
            
        ingest_gb = float(ingest_match.group(1))
        
        # 计算Write Amplification: WA = (compaction_write + ingest) / ingest
        if ingest_gb > 0:
            write_amplification = (compaction_write_gb + ingest_gb) / ingest_gb
        else:
            print(f"警告：ingest数据为0，无法计算WA，跳过时间戳 {timestamp}")
            continue
        
        # 添加到结果列表
        results.append([entry_number, throughput, write_amplification])
        entry_number += 1
        
        print(f"Entry {entry_number-1}: Timestamp={timestamp}, Throughput={throughput:.1f}, "
              f"Compaction={compaction_write_gb:.2f}GB, Ingest={ingest_gb:.2f}GB, WA={write_amplification:.4f}")
    
    # 写入TSV文件（使用制表符分隔）
    with open(output_csv_path, 'w', newline='', encoding='utf-8') as tsvfile:
        writer = csv.writer(tsvfile, delimiter='\t')
        # 写入表头
        writer.writerow(['Entry_Number', 'Throughput_ops_per_sec', 'WriteAmplification'])
        # 写入数据
        writer.writerows(results)
    
    print(f"\n解析完成！共提取了 {len(results)} 组数据")
    print(f"结果已保存到: {output_csv_path}")
    
    return results

def preview_results(results, num_rows=5):
    """
    预览提取的结果
    """
    print(f"\n前 {min(num_rows, len(results))} 行数据预览:")
    print("编号\t吞吐量(ops/sec)\t写放大")
    print("-" * 40)
    for i in range(min(num_rows, len(results))):
        entry_num, throughput, wa = results[i]
        print(f"{entry_num}\t{throughput:.1f}\t\t{wa:.4f}")

# 使用示例
if __name__ == "__main__":
    # 获取当前脚本所在的目录
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # 设置输入和输出文件路径（都在当前脚本目录下）
    input_log_file = os.path.join(current_dir, "RocksDB_1B_val128_zipf1.1_mem64MB_stats10000000.log")  # 输入日志文件
    output_csv_file = os.path.join(current_dir, "rocksdb_performance_data.csv")  # 输出CSV文件
    
    print(f"当前工作目录: {current_dir}")
    print(f"输入文件路径: {input_log_file}")
    print(f"输出文件路径: {output_csv_file}")
    print("-" * 50)
    
    try:
        # 解析日志文件
        results = parse_rocksdb_log(input_log_file, output_csv_file)
        
        # 预览结果
        if results:
            preview_results(results)
            
            # 显示统计信息
            throughputs = [r[1] for r in results]
            write_amps = [r[2] for r in results]
            
            print(f"\n统计信息:")
            print(f"吞吐量范围: {min(throughputs):.1f} - {max(throughputs):.1f} ops/sec")
            print(f"写放大范围: {min(write_amps):.4f} - {max(write_amps):.4f}")
            print(f"平均吞吐量: {sum(throughputs)/len(throughputs):.1f} ops/sec")
            print(f"平均写放大: {sum(write_amps)/len(write_amps):.4f}")
        else:
            print("未找到匹配的数据，请检查日志文件格式")
            
    except FileNotFoundError:
        print(f"错误: 找不到文件 '{input_log_file}'")
        print("请确保在脚本同目录下放置名为 'rocksdb_log.txt' 的日志文件")
    except Exception as e:
        print(f"处理过程中出现错误: {e}")