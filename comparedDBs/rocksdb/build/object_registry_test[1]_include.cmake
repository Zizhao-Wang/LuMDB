if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/object_registry_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/object_registry_test[1]_tests.cmake")
else()
  add_test(object_registry_test_NOT_BUILT object_registry_test_NOT_BUILT)
endif()
