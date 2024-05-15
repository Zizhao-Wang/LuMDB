# CMake generated Testfile for 
# Source directory: /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb
# Build directory: /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;366;add_test;/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;0;")
add_test(c_test "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;392;add_test;/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;395;leveldb_test;/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;0;")
add_test(env_posix_test "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;392;add_test;/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;403;leveldb_test;/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
