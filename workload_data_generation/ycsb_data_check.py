import pandas as pd

def check_and_clean_ycsb_data(file_path):
    try:
        # 定义操作类型的映射
        operation_types = {
            "ycsb_a": ['READ', 'UPDATE'],
            "ycsb_b": ['READ', 'UPDATE'],
            "ycsb_c": ['READ'],
            "ycsb_d": ['READ', 'INSERT'],
            "ycsb_e": ['SCAN', 'INSERT'],
            "ycsb_f": ['READ', 'RMW']  # RMW = Read-Modify-Write
        }

        # 根据文件名判断类型
        file_name = file_path.split('/')[-1].lower()
        workload_type = file_name.split('_')[1]  # 例如 "ycsb_a_workload.csv" -> "a"
        valid_operations = operation_types.get(f"ycsb_{workload_type}", None)

        if not valid_operations:
            print(f"错误：无法识别的工作负载类型 '{workload_type}'，请检查文件名是否正确。")
            return

        print(f"检测到工作负载类型:YCSB-{workload_type.upper()}，允许的操作类型：{valid_operations}")

        # 加载 CSV 文件
        data = pd.read_csv(file_path, dtype=str)

        # 记录原始数据总行数
        original_row_count = len(data)

        # 检查文件是否包含 "Operation" 和 "Key" 两列
        if 'Operation' not in data.columns or 'Key' not in data.columns:
            print("错误：文件中缺少 'Operation' 或 'Key' 列。")
            return

        # 检查 "Operation" 列是否包含有效操作类型
        invalid_operations = data[~data['Operation'].isin(valid_operations)]
        if not invalid_operations.empty:
            print(f"发现 {len(invalid_operations)} 行无效的 'Operation' 值，已删除：")
            print(invalid_operations)
            # 删除无效的操作行
            data = data[data['Operation'].isin(valid_operations)]

        # 检查 "Key" 列是否全为数字
        invalid_keys = data[~data['Key'].apply(lambda x: str(x).isdigit())]
        if not invalid_keys.empty:
            print(f"发现 {len(invalid_keys)} 行无效的 'Key' 值，已删除：")
            print(invalid_keys)
            # 删除无效的键行
            data = data[data['Key'].apply(lambda x: str(x).isdigit())]

        # 清理后的数据总行数
        cleaned_row_count = len(data)

        # 输出清理统计信息
        print(f"清理统计：")
        print(f"  原始文件总行数：{original_row_count}")
        print(f"  删除的无效行数：{original_row_count - cleaned_row_count}")
        print(f"  清理后的总行数：{cleaned_row_count}")

        # 示例显示清理后的数据前几行
        print("清理后的文件示例数据：")
        print(data.head())

        # 覆盖保存清理后的文件
        data.to_csv(file_path, index=False)
        print(f"清理后的数据已保存到原文件：{file_path}")

    except Exception as e:
        print(f"发生错误：{e}")

# 文件路径
file_path = '/home/jeff-wang/workloads/ycsb_a_workload.csv'

# 执行检查和清理
check_and_clean_ycsb_data(file_path)
