if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_basic_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_basic_test[1]_tests.cmake")
else()
  add_test(db_basic_test_NOT_BUILT db_basic_test_NOT_BUILT)
endif()
