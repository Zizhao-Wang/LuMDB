if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/file_reader_writer_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/file_reader_writer_test[1]_tests.cmake")
else()
  add_test(file_reader_writer_test_NOT_BUILT file_reader_writer_test_NOT_BUILT)
endif()
