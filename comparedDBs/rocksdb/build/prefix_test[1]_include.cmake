if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/prefix_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/prefix_test[1]_tests.cmake")
else()
  add_test(prefix_test_NOT_BUILT prefix_test_NOT_BUILT)
endif()
