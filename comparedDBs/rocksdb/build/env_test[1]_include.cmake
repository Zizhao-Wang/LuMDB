if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/env_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/env_test[1]_tests.cmake")
else()
  add_test(env_test_NOT_BUILT env_test_NOT_BUILT)
endif()
