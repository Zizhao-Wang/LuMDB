if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_merge_operator_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_merge_operator_test[1]_tests.cmake")
else()
  add_test(db_merge_operator_test_NOT_BUILT db_merge_operator_test_NOT_BUILT)
endif()
