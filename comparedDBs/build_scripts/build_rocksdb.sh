mkdir ./rocksdb/release
cd ./rocksdb/release && cmake -DCMAKE_BUILD_TYPE=Release  .. && make -j32 && cd ../../