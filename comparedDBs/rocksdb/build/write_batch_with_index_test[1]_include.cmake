if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/write_batch_with_index_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/write_batch_with_index_test[1]_tests.cmake")
else()
  add_test(write_batch_with_index_test_NOT_BUILT write_batch_with_index_test_NOT_BUILT)
endif()
