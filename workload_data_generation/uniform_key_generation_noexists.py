import polars as pl
import os

# 起始值
start_value = 18446744073709551615
# 数值数量（您可以根据需要调整）
num_values = 1000  # 比如生成100万个数

key_size = 16
val_size = 128

# 生成递减序列，并将整数转换为字符串
keys = [str(start_value - i) for i in range(num_values)]

# 创建 DataFrame 并写入CSV
data = pl.DataFrame({
    'Key': keys,
})


# 文件名
file_name = f'/home/jeff-wang/workloads/uniform_read_keys{key_size}_value{val_size}_1Thousand_noexists.csv'

# 检查文件是否存在，如果存在则删除
if os.path.exists(file_name):
    os.remove(file_name)
    print(f"Existing file '{file_name}' has been deleted.")

# 保存到CSV文件
data.write_csv(file_name)
print(f"Data written to {file_name}")
