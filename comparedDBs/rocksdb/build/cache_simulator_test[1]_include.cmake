if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/cache_simulator_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/cache_simulator_test[1]_tests.cmake")
else()
  add_test(cache_simulator_test_NOT_BUILT cache_simulator_test_NOT_BUILT)
endif()
