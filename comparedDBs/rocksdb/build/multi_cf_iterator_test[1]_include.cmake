if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/multi_cf_iterator_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/multi_cf_iterator_test[1]_tests.cmake")
else()
  add_test(multi_cf_iterator_test_NOT_BUILT multi_cf_iterator_test_NOT_BUILT)
endif()
