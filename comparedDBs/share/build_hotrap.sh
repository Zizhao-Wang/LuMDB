# Check if the hotrap/release directory exists
if [ -d "./hotrap/release" ]; then
    # If it exists, remove the directory
    rm -rf ./hotrap/release
fi

# Create the hotrap/release directory
mkdir ./hotrap/release

# Change directory, configure to only build the core library, and make it
cd ./hotrap/release && \
cmake -DCMAKE_BUILD_TYPE=Debug -DUSE_DIO=ON  .. &&  make -j30 rocksdb && cd ../../
