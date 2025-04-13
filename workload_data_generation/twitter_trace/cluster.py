import random


CLUSTER_NUM = 37 # 这里可以修改为任何需要的 cluster 数字
input_path = f'/mnt/nvm/second_cluster{CLUSTER_NUM}.sort'
output_path = f"/mnt/nvm/cluster{CLUSTER_NUM}_modified.sort"

total_lines = 10_000_000
write_ratio = 0.8  # 80% set, 20% get

# 先生成 1000 万个操作类型列表（顺序打乱）
ops = ['set'] * int(total_lines * write_ratio) + ['get'] * (total_lines - int(total_lines * write_ratio))
random.shuffle(ops)

with open(input_path, 'r') as fin, open(output_path, 'w') as fout:
    for idx, line in enumerate(fin):
        if idx >= total_lines:
            break

        parts = line.strip().split(',')
        if len(parts) >= 7:
            parts[5] = ops[idx]  # 替换第六列为 set 或 get
            fout.write(','.join(parts) + '\n')
        else:
            fout.write(line)  # 不合法格式直接保留

print("✅ 完成前 1000 万行替换并保存到", output_path)
