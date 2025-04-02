#!/bin/bash

# Set paths to RocksDB
ROCKSDB_INCLUDE=/home/jeff-wang/LuMDB/comparedDBs/share/hotrap/include
ROCKSDB_LIB=/home/jeff-wang/LuMDB/comparedDBs/share/hotrap/release

# Optional: clean previous build
if [ -d "./RALT/build" ]; then
    rm -rf ./RALT/build
fi

# Create and enter build directory
mkdir ./RALT/build && cd ./RALT/build

# Run CMake with custom RocksDB paths
cmake .. \
    -DROCKSDB_INCLUDE=${ROCKSDB_INCLUDE} \
    -DROCKSDB_LIB=${ROCKSDB_LIB}

# Build the project
make -j30 ralt
