if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/blob_counting_iterator_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/blob_counting_iterator_test[1]_tests.cmake")
else()
  add_test(blob_counting_iterator_test_NOT_BUILT blob_counting_iterator_test_NOT_BUILT)
endif()