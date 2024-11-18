#!/bin/bash

# 定义 cgroup 名称和对应的内存和 CPU 限制
cgroups=(
    "group4 4G 100"
    "group8 8G 100"
    "group16 16G 100"
    "group32 32G 100"
    "group64 64G 100"
    "group128 128G 100"
)

# 创建 cgroup 并设置内存和 CPU 限制
for cgroup in "${cgroups[@]}"; do
    read -r name memory cpu <<< "$cgroup"
    
    # 创建 cgroup
    sudo cgcreate -g memory,cpu:/$name
    
    # 设置内存限制
    mem_bytes=$(echo $memory | sed 's/M/*1024*1024/' | sed 's/G/*1024*1024*1024/' | bc)
    sudo echo $mem_bytes > /sys/fs/cgroup/$name/memory.max

    # 设置 CPU 限制
    # 在 cgroup v2 中, 使用 'cpu.max' 设置CPU限额, 格式为 'quota period'
    cpu_quota=$((cpu * 1000))
    sudo echo "$cpu_quota 100000" >  /sys/fs/cgroup/$name/cpu.max

    echo "Created cgroup $name with $memory memory limit and $cpu% CPU limit."
done

echo "All cgroups have been created and configured."
