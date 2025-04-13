z# Check if the KV_stores/hotdb/twitter_release directory exists
if [ -d "../hotdb/twitter_release" ]; then
    # If it exists, remove the directory
    rm -rf ../hotdb/twitter_release
fi
# Create the KV_stores/hotdb/twitter_release directory
mkdir ../hotdb/twitter_release
# Change directory to KV_stores/hotdb/twitter_release, configure the build for twitter_release, make the build with 32 jobs, and then return to the original directory
cd ../hotdb/twitter_release && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../