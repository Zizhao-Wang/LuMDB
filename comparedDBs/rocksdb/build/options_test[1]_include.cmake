if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/options_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/options_test[1]_tests.cmake")
else()
  add_test(options_test_NOT_BUILT options_test_NOT_BUILT)
endif()
