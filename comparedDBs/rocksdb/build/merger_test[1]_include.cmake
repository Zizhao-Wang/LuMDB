if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/merger_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/merger_test[1]_tests.cmake")
else()
  add_test(merger_test_NOT_BUILT merger_test_NOT_BUILT)
endif()
