if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/data_block_hash_index_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/data_block_hash_index_test[1]_tests.cmake")
else()
  add_test(data_block_hash_index_test_NOT_BUILT data_block_hash_index_test_NOT_BUILT)
endif()
