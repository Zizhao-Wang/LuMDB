if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/work_queue_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/work_queue_test[1]_tests.cmake")
else()
  add_test(work_queue_test_NOT_BUILT work_queue_test_NOT_BUILT)
endif()