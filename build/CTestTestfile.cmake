# CMake generated Testfile for 
# Source directory: /home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb
# Build directory: /home/wangzizhao/WorkloadAnalysis/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/home/wangzizhao/WorkloadAnalysis/build/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;366;add_test;/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;0;")
add_test(c_test "/home/wangzizhao/WorkloadAnalysis/build/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;392;add_test;/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;395;leveldb_test;/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;0;")
add_test(env_posix_test "/home/wangzizhao/WorkloadAnalysis/build/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;392;add_test;/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;403;leveldb_test;/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
