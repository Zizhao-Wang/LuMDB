if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/io_posix_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/io_posix_test[1]_tests.cmake")
else()
  add_test(io_posix_test_NOT_BUILT io_posix_test_NOT_BUILT)
endif()