rm -rf */release
mkdir ./pebblesdb/release
cd ./pebblesdb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j32 && cd ../../
