if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/block_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/block_test[1]_tests.cmake")
else()
  add_test(block_test_NOT_BUILT block_test_NOT_BUILT)
endif()
