if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/block_fetcher_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/block_fetcher_test[1]_tests.cmake")
else()
  add_test(block_fetcher_test_NOT_BUILT block_fetcher_test_NOT_BUILT)
endif()
