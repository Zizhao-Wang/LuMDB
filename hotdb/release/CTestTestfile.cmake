# CMake generated Testfile for 
# Source directory: /home/jeff-wang/LuMDB/hotdb
# Build directory: /home/jeff-wang/LuMDB/hotdb/release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/home/jeff-wang/LuMDB/hotdb/release/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;379;add_test;/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;0;")
add_test(c_test "/home/jeff-wang/LuMDB/hotdb/release/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;405;add_test;/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;408;leveldb_test;/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;0;")
add_test(env_posix_test "/home/jeff-wang/LuMDB/hotdb/release/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;405;add_test;/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;416;leveldb_test;/home/jeff-wang/LuMDB/hotdb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
