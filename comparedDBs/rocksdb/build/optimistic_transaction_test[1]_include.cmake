if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/optimistic_transaction_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/optimistic_transaction_test[1]_tests.cmake")
else()
  add_test(optimistic_transaction_test_NOT_BUILT optimistic_transaction_test_NOT_BUILT)
endif()