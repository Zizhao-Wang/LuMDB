import os
import re
from typing import Tuple, Optional, Dict, List

# --- 全局配置：自定义输出的集群顺序 ---
# 您可以根据需要修改这个列表的顺序
CLUSTER_ORDER = ['cluster35', 'cluster13', 'cluster40', 'cluster30', 'cluster1']

def parse_log_file(file_path: str) -> Tuple[Optional[str], Optional[str]]:
    """
    解析单个日志文件，提取 RemoveTopk 操作数和最后出现的 Cumulative compaction 写入量。

    Args:
        file_path (str): 日志文件的完整路径。

    Returns:
        A tuple containing (operations_count, compaction_write_size).
        如果找不到对应的值，则为 None。
    """
    regex_remove_topk = re.compile(r"RemoveTopk\s*:\s*.*?(\d{5,})\s+operations")
    regex_compaction = re.compile(r"Cumulative compaction:.*?([\d.]+\s+(?:GB|MB|KB))\s+write")

    operations_count = None
    compaction_write_size = None

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                if operations_count is None:
                    match_topk = regex_remove_topk.search(line)
                    if match_topk:
                        operations_count = match_topk.group(1)

                match_compaction = regex_compaction.search(line)
                if match_compaction:
                    compaction_write_size = match_compaction.group(1).strip()
    except Exception as e:
        print(f"Error processing file {file_path}: {e}")

    return operations_count, compaction_write_size

def process_directory(root_dir: str, removal_value: int) -> Dict[str, Dict[str, str]]:
    """
    处理指定目录下的所有匹配 removal_value 的日志文件。

    Args:
        root_dir (str): 要处理的根目录。
        removal_value (int): 要查找的移除值 (例如 10, 100, 200)。

    Returns:
        一个包含所有结果的字典。
    """
    results = {}
    target_suffix = f"_Removal{removal_value}.log"
    print(f"Starting to process logs ending with '{target_suffix}' in directory: {root_dir}\n")

    for subdir, _, files in os.walk(root_dir):
        for file in files:
            if file.endswith(target_suffix):
                full_path = os.path.join(subdir, file)
                relative_path = os.path.relpath(full_path, root_dir)
                ops_count, compaction_size = parse_log_file(full_path)
                results[relative_path] = {
                    "RemoveTopk_Operations": ops_count,
                    "Compaction_Write_Size": compaction_size
                }
    return results

def get_cluster_sort_key(file_path: str) -> int:
    """
    为文件路径生成一个用于排序的键。
    它会根据 CLUSTER_ORDER 列表返回集群的索引。
    """
    for i, cluster_name in enumerate(CLUSTER_ORDER):
        if cluster_name in file_path:
            return i
    # 如果文件路径中的集群不在我们的排序列表中，则放到最后
    return len(CLUSTER_ORDER)

if __name__ == "__main__":
    # --- 脚本将自动搜索其所在的目录 ---
    # 您也可以将其修改为具体的路径，例如: r"D:\data\Logs"
    root_directory = os.path.dirname(os.path.abspath(__file__))

    # --- 请在这里指定要处理的 Removal 值 ---
    removal_value_to_process = 800

    if not os.path.isdir(root_directory):
        print(f"Error: Directory '{root_directory}' does not exist.")
        print("Please edit the 'root_directory' variable in the script to point to your logs folder.")
    else:
        all_results = process_directory(root_directory, removal_value_to_process)

        if not all_results:
            print(f"No log files ending with '_Removal{removal_value_to_process}.log' were found.")
        else:
            # 根据自定义顺序对结果进行排序
            sorted_paths = sorted(all_results.keys(), key=get_cluster_sort_key)

            # --- 格式化输出 ---
            print("--- Parsing Results ---")
            # 打印表头，使用 f-string 进行左对齐
            print(f"{'#':<5}{'RemoveTopk Operations':<25}{'Compaction Write Size':<25}")
            print("-" * 55) # 打印分隔线

            # 遍历排序后的路径并打印数据
            for i, path in enumerate(sorted_paths, 1):
                data = all_results[path]
                
                # *** FIX STARTS HERE ***
                # 获取值，如果值是 None，则替换为 'N/A' 字符串以避免 TypeError
                ops = data.get('RemoveTopk_Operations')
                compaction = data.get('Compaction_Write_Size')

                ops_str = ops if ops is not None else 'N/A'
                compaction_str = compaction if compaction is not None else 'N/A'
                
                # 打印数据行
                print(f"{i:<5}{ops_str:<25}{compaction_str:<25}")
                # *** FIX ENDS HERE ***
            
            print("\n--- End of Report ---")
