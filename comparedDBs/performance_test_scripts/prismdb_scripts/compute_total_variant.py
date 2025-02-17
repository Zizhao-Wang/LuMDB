import pandas as pd
import polars as pl

# 假设文件路径和文件格式
file_path = '/home/jeff-wang/workloads/zipf_keys1.0k_zipf1.1.csv'  # 请替换为您的文件路径

# # 读取CSV文件，假定第一行是列名
# data = pd.read_csv(file_path, delimiter=';')

# # 统计每个数字出现的次数
# counts = data['Key'].value_counts()

# # 去重并按出现次数升序排序
# sorted_counts = counts.sort_values()

keys = pl.read_csv(file_path, separator=",")

# 需要移除的数据列表
# base_remove_keys = [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70]


# 初始化 base_remove_keys 列表，包含从1到100的整数
base_remove_keys = list(range(1, 300))

# 输出以确认列表内容
print("base_remove_keys:", base_remove_keys)


# 使用过滤操作移除指定的数据
# filtered_keys = keys.filter(~keys["Key"].is_in(remove_keys))

# key_frequencies = filtered_keys.group_by("Key").agg(pl.len().alias("Frequency"))
# key_frequencies_sorted = key_frequencies.sort("Frequency")

# # 为每个数字赋值
# values_mapping = {}
# current_value = -1
# last_count = 0

# for count in key_frequencies_sorted['Frequency'].unique().to_list():
#     if count == 1:
#         values_mapping[count] = 0
#     else:
#         if count != last_count:
#             current_value += 1
#         values_mapping[count] = current_value
#     last_count = count

# # 将映射字典转换为DataFrame，并确保数据类型匹配
# mapping_df = pl.DataFrame({
#     "Frequency": list(values_mapping.keys()),
#     "Value": list(values_mapping.values())
# }).cast({"Frequency": pl.UInt64})

# # 将频率值映射到新的值通过DataFrame连接
# key_frequencies_mapped = key_frequencies_sorted.join(mapping_df, on="Frequency")

# # 重新整合原始数据和频率数据，为每个原始数据赋予对应的Value
# result = keys.join(key_frequencies_mapped, on="Key")


# 重新整合原始数据和频率数据，直接使用频率作为值
# result = keys.join(key_frequencies_sorted, on="Key")

# # 计算变差
# result = result.with_columns([
#     pl.col("Frequency").diff().fill_null(0).abs().alias("Difference")
# ])

# # 计算变差的总和
# total_difference_sum = result["Difference"].sum()


# # 获取“Value”列中数据的个数
# count_values = result["Frequency"].len()

# # 计算平均差值
# average_difference = total_difference_sum / count_values

# # 输出总和
# # 输出详细信息
# print("Total sum of differences:", total_difference_sum)
# print("Number of data points in 'Frequency':", count_values)
# print("Average difference:", average_difference)

# # 写入到新的CSV文件，包含Key, Value和Difference
# output_file_path = f'./output_keys_values_differencesNo{len(remove_keys)}.csv'  # 您可以自定义输出文件的路径
# result.select(["Key", "Difference"]).write_csv(output_file_path)
# print(f"Results saved to {output_file_path}.")


# 循环逐步增加移除的键，并计算变差
for i in range(0, len(base_remove_keys) + 1):
    remove_keys = base_remove_keys[:i]
    # 使用过滤操作移除指定的数据
    filtered_keys = keys.filter(~keys["Key"].is_in(remove_keys))

    # 分组并计算每个键的频率
    key_frequencies = keys.group_by("Key").agg(pl.len().alias("Frequency"))

    # 筛选出频率至少为两次的键
    frequent_keys = key_frequencies.filter(pl.col("Frequency") >= 2)

   # 获取对应的Key列，计算这些键的方差
    # 我们需要重新从原始过滤数据中获取这些键的具体值
    key_values_for_variance = filtered_keys.filter(pl.col("Key").is_in(frequent_keys["Key"]))

    # 计算方差
    variance = key_values_for_variance["Key"].var()

    # 输出结果
    print(variance)

    # total_count = filtered_keys.count()
    # print(total_count)

    # 分组并计算每个键的频率
    # key_frequencies = filtered_keys.group_by("Key").agg(pl.len().alias("Frequency"))
    # key_frequencies_sorted = key_frequencies.sort("Frequency")

    # key_frequencies_sorted = key_frequencies_sorted.with_columns(
    #     (pl.col("Frequency") / total_count).alias("Frequency Proportion")
    # )

    # # 重新整合原始数据和频率数据，为每个原始数据赋予对应的Value
    # result = keys.join(key_frequencies_sorted, on="Key")


    # # 计算变差
    # result = result.with_columns([
    #     pl.col("Frequency Proportion").diff().fill_null(0).abs().alias("Difference")
    # ])

    # # 计算变差的总和
    # total_difference_sum = result["Difference"].sum()

    # # 获取“Frequency”列中数据的个数
    # count_values = result["Frequency"].len()

    # # 计算平均差值
    # average_difference = total_difference_sum / count_values if count_values else 0

    # # 输出结果
    # # print(f"After removing keys: {remove_keys}")
    # # print("Total sum of differences:", total_difference_sum)
    # # print("Number of data points in 'Frequency':", count_values)
    # print("Average difference:", average_difference)
    # print("-" * 40)