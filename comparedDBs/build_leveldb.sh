mkdir ./leveldb/release
cd ./leveldb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j32 && cd ../../
