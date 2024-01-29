# # Check if the pebblesdb/release directory exists
# if [ -d "../KV_stores/pebblesdb/release" ]; then
#     # If it exists, remove the directory
#     rm -rf ../KV_stores/pebblesdb/release
# fi
# # Create the pebblesdb/release directory
# mkdir ../KV_stores/pebblesdb/release
# # Change directory to pebblesdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
# cd ../KV_stores/pebblesdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../



# Check if the KV_stores/leveldb/release directory exists
if [ -d "../KV_stores/leveldb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../KV_stores/leveldb/release
fi
# Create the KV_stores/leveldb/release directory
mkdir ../KV_stores/leveldb/release
# Change directory to KV_stores/leveldb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../KV_stores/leveldb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../


# # Check if the rocksdb/release directory exists
# if [ -d "../KV_stores/rocksdb/release" ]; then
#     # If it exists, remove the directory
#     rm -rf ../KV_stores/rocksdb/release
# fi
# # Create the rocksdb/release directory
# mkdir ../KV_stores/rocksdb/release
# # Change directory to rocksdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
# cd ../KV_stores/rocksdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../

