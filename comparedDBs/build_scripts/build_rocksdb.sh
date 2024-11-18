# Check if the rocksdb/release directory exists
if [ -d "../rocksdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../rocksdb/release
fi
# Create the rocksdb/release directory
mkdir ../rocksdb/release
# Change directory to rocksdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../rocksdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../