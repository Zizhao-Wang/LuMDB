if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/autovector_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/autovector_test[1]_tests.cmake")
else()
  add_test(autovector_test_NOT_BUILT autovector_test_NOT_BUILT)
endif()
