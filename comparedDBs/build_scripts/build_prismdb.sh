# Check if the KV_stores/prismdb/release directory exists
if [ -d "../prismdb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../prismdb/release
fi
# Create the KV_stores/prismdb/release directory
mkdir ../prismdb/release
# Change directory to KV_stores/prismdb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../prismdb/release && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../