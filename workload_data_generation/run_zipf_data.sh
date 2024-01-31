#!/bin/bash

BASE_VALUE_SIZE=128
billion=3000000000

# 定义Python解释器和脚本路径
PYTHON_BIN="/home/wangzizhao/anaconda3/envs/dbenv/bin/python"
ZIPF_KEY_GEN_SCRIPT="/home/wangzizhao/WorkloadAnalysis/workload_data_generation/zipf_key_generation.py"

base_num=$billion
for value_size in 256 512 1024 2048; do 
    num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
    for zipf_a in 1.01 1.1 1.2 1.3 1.4 1.5; do

        $PYTHON_BIN $ZIPF_KEY_GEN_SCRIPT $zipf_a $num_entries            
    done
done



