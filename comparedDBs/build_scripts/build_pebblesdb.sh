# Check if the rocksdb/release directory exists
if [ -d "../pebblesdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../pebblesdb/release
fi
# Create the rocksdb/release directory
mkdir ../pebblesdb/release
# Change directory to rocksdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../pebblesdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../