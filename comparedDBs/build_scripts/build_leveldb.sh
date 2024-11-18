# Check if the KV_stores/leveldb/release directory exists
if [ -d "../leveldb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../leveldb/release
fi
# Create the KV_stores/leveldb/release directory
mkdir ../leveldb/release
# Change directory to KV_stores/leveldb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../leveldb/release && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../