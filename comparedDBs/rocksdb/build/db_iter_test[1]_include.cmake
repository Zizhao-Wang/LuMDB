if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_iter_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_iter_test[1]_tests.cmake")
else()
  add_test(db_iter_test_NOT_BUILT db_iter_test_NOT_BUILT)
endif()
