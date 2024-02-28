#!/bin/bash

# Print start message
echo "Starting to build all KEY-VALUE Stores..."

# Build LevelDB
echo "Building LevelDB..."
./build_leveldb.sh
echo "LevelDB build completed."

# Build PebblesDB
echo "Building PebblesDB..."
./build_pebblesdb.sh
echo "PebblesDB build completed."

# Build RocksDB
echo "Building RocksDB..."
./build_rocksdb.sh
echo "RocksDB build completed."

# Build WiscKey
echo "Building WiscKey..."
./build_wisckey.sh
echo "WiscKey build completed."

echo "All databases have been built."
