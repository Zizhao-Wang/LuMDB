# Check if the KV_stores/hotdb/release_2ppl directory exists
if [ -d "../hotdb/release_2ppl" ]; then
    # If it exists, remove the directory
    rm -rf ../hotdb/release_2ppl
fi
# Create the KV_stores/hotdb/release_2ppl directory
mkdir ../hotdb/release_2ppl
# Change directory to KV_stores/hotdb/release_2ppl, configure the build for release_2ppl, make the build with 32 jobs, and then return to the original directory
cd ../hotdb/release_2ppl && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../