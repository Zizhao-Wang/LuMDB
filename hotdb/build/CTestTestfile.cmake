# CMake generated Testfile for 
# Source directory: /home/jeff-wang/WorkloadAnalysis/hotdb
# Build directory: /home/jeff-wang/WorkloadAnalysis/hotdb/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/home/jeff-wang/WorkloadAnalysis/hotdb/build/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;368;add_test;/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;0;")
add_test(c_test "/home/jeff-wang/WorkloadAnalysis/hotdb/build/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;394;add_test;/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;397;leveldb_test;/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;0;")
add_test(env_posix_test "/home/jeff-wang/WorkloadAnalysis/hotdb/build/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;394;add_test;/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;405;leveldb_test;/home/jeff-wang/WorkloadAnalysis/hotdb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
