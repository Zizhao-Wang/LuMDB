#!/bin/bash

BASE_VALUE_SIZE=128
billion=2000000000

# 定义Python解释器和脚本路径
PYTHON_BIN="/home/wangzizhao/anaconda3/envs/dbenv/bin/python"
ZIPF_KEY_GEN_SCRIPT="/home/jeff-wang/WorkloadAnalysis/workload_data_generation/zipf_key_generation.py"

base_num=$billion
for value_size in 256 512 1024 2048; do 
    num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
    for zipf_a in 1.01 1.1 1.2 1.3 1.4 1.5; do
        $PYTHON_BIN $ZIPF_KEY_GEN_SCRIPT $zipf_a $num_entries   
        echo "Finished zipf_key_generation.py with zipf_a $zipf_a and num_entries $num_entries"        
        sleep 3 
    done
done



