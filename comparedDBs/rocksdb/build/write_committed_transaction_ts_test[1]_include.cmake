if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/write_committed_transaction_ts_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/write_committed_transaction_ts_test[1]_tests.cmake")
else()
  add_test(write_committed_transaction_ts_test_NOT_BUILT write_committed_transaction_ts_test_NOT_BUILT)
endif()