if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/wal_edit_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/wal_edit_test[1]_tests.cmake")
else()
  add_test(wal_edit_test_NOT_BUILT wal_edit_test_NOT_BUILT)
endif()
