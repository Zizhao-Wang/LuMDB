# Check if the pebblesdb/release directory exists
if [ -d "./pebblesdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ./pebblesdb/release
fi
# Create the pebblesdb/release directory
mkdir ./pebblesdb/release
# Change directory to pebblesdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ./pebblesdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j32 && cd ../../



# Check if the KV_stores/leveldb/release directory exists
if [ -d "../KV_stores/leveldb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../KV_stores/leveldb/release
fi
# Create the KV_stores/leveldb/release directory
mkdir ../KV_stores/leveldb/release
# Change directory to KV_stores/leveldb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../KV_stores/leveldb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j32 && cd ../../


# Check if the rocksdb/release directory exists
if [ -d "./rocksdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ./rocksdb/release
fi
# Create the rocksdb/release directory
mkdir ./rocksdb/release
# Change directory to rocksdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ./rocksdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j32 && cd ../../

