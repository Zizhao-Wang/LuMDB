if(EXISTS "/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_blob_compaction_test[1]_tests.cmake")
  include("/home/jeff-wang/WorkloadAnalysis/comparedDBs/rocksdb/build/db_blob_compaction_test[1]_tests.cmake")
else()
  add_test(db_blob_compaction_test_NOT_BUILT db_blob_compaction_test_NOT_BUILT)
endif()
