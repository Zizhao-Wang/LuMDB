if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/string_util_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/string_util_test[1]_tests.cmake")
else()
  add_test(string_util_test_NOT_BUILT string_util_test_NOT_BUILT)
endif()
