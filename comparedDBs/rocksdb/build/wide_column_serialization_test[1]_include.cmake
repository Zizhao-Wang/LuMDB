if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/wide_column_serialization_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/wide_column_serialization_test[1]_tests.cmake")
else()
  add_test(wide_column_serialization_test_NOT_BUILT wide_column_serialization_test_NOT_BUILT)
endif()
