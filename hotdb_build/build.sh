z# Check if the KV_stores/hotdb/release directory exists
if [ -d "../hotdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../hotdb/release
fi
# Create the KV_stores/hotdb/release directory
mkdir ../hotdb/release
# Change directory to KV_stores/hotdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../hotdb/release && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../