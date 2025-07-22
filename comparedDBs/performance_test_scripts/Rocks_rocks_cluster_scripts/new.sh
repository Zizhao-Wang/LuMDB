#!/bin/bash

# ========================================
#         Global Configurations
# ========================================

# Set the total memtable size
# For example: TOTAL_MEMTABLE_SIZE=$((1024 * 1024 * 1024)) # 1GB
TOTAL_MEMTABLE_SIZE=${TOTAL_MEMTABLE_SIZE:-$((64*1024*1024))} # Default to 1GB if not set

# Declare associative arrays to hold the results
declare -A ROCKSDB1_MEMTABLE_MAP
declare -A ROCKSDB2_MEMTABLE_MAP

echo "Total Memtable Size: $TOTAL_MEMTABLE_SIZE"
echo "========================================"

# ========================================
#         Cluster 40 Configuration
# ========================================
echo "Configuring Cluster 40..."

# Define thresholds and their corresponding hot data percentages for Cluster 40
# Thresholds are the number of top keys (e.g., 10, 100, 500)
# Percentages are the corresponding portion of hot data (e.g., 0.0028 for 0.28%)
c40_thresholds=(10 100 500 1000 2000 5000 10000 20000 50000 100000)
c40_percentages=(0.0028 0.0261 0.0819 0.1158 0.1565 0.2199 0.2736 0.3316 0.4133 0.4773)

# --- Calculations for Cluster 40 ---

# Initial state: all data in ROCKSDB1
ROCKSDB1_MEMTABLE_MAP["40_0"]=${TOTAL_MEMTABLE_SIZE}
ROCKSDB2_MEMTABLE_MAP["40_0"]=0

# Loop through each threshold to calculate memtable distribution
for i in "${!c40_thresholds[@]}"; do
    threshold=${c40_thresholds[$i]}
    percentage=${c40_percentages[$i]}
    
    # ROCKSDB2 (hot data) size calculation
    rocksdb2_size=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * $percentage / 1" | bc)
    ROCKSDB2_MEMTABLE_MAP["40_${threshold}"]=$rocksdb2_size
    
    # ROCKSDB1 (cold data) size calculation
    rocksdb1_size=$(($TOTAL_MEMTABLE_SIZE - $rocksdb2_size))
    ROCKSDB1_MEMTABLE_MAP["40_${threshold}"]=$rocksdb1_size
    
    echo "Cluster 40 - Threshold ${threshold}:"
    echo "  Hot Data (RocksDB2): ${rocksdb2_size} bytes"
    echo "  Cold Data (RocksDB1): ${rocksdb1_size} bytes"
done

echo "========================================"

# ========================================
#         Cluster 49 Configuration
# ========================================
echo "Configuring Cluster 49..."

# Define thresholds and their corresponding hot data percentages for Cluster 49
c49_thresholds=(10 100 500 1000 2000 5000 10000 20000 50000 100000)
c49_percentages=(0.0011 0.0066 0.0230 0.0383 0.0621 0.1132 0.1732 0.2582 0.4188 1.0)
# Note: I used the percentages from your comment block, as they seemed more up-to-date
# than the ones in the original script code for Cluster 49.

# --- Calculations for Cluster 49 ---

# Initial state: all data in ROCKSDB1
ROCKSDB1_MEMTABLE_MAP["49_0"]=${TOTAL_MEMTABLE_SIZE}
ROCKSDB2_MEMTABLE_MAP["49_0"]=0

# Loop through each threshold to calculate memtable distribution
for i in "${!c49_thresholds[@]}"; do
    threshold=${c49_thresholds[$i]}
    percentage=${c49_percentages[$i]}
    
    # ROCKSDB2 (hot data) size calculation
    rocksdb2_size=$(echo "scale=0; $TOTAL_MEMTABLE_SIZE * $percentage / 1" | bc)
    ROCKSDB2_MEMTABLE_MAP["49_${threshold}"]=$rocksdb2_size
    
    # ROCKSDB1 (cold data) size calculation
    rocksdb1_size=$(($TOTAL_MEMTABLE_SIZE - $rocksdb2_size))
    ROCKSDB1_MEMTABLE_MAP["49_${threshold}"]=$rocksdb1_size

    echo "Cluster 49 - Threshold ${threshold}:"
    echo "  Hot Data (RocksDB2): ${rocksdb2_size} bytes"
    echo "  Cold Data (RocksDB1): ${rocksdb1_size} bytes"
done

echo "========================================"
echo "Configuration complete."

# You can optionally print the final maps to verify
# echo "Final ROCKSDB1 Map:"
# for k in "${!ROCKSDB1_MEMTABLE_MAP[@]}"; do echo "$k => ${ROCKSDB1_MEMTABLE_MAP[$k]}"; done
# echo "Final ROCKSDB2 Map:"
# for k in "${!ROCKSDB2_MEMTABLE_MAP[@]}"; do echo "$k => ${ROCKSDB2_MEMTABLE_MAP[$k]}"; done