if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/timer_queue_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/timer_queue_test[1]_tests.cmake")
else()
  add_test(timer_queue_test_NOT_BUILT timer_queue_test_NOT_BUILT)
endif()