// Copyright (c) 2011 Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.



#ifndef STORAGE_LEVELDB_DB_PARTITION_H_
#define STORAGE_LEVELDB_DB_PARTITION_H_

#include "leveldb/slice.h"
#include "db/version_edit.h"
#include "db/dbformat.h"
#include "memtable.h"

namespace leveldb {

  struct  partition_leveling_data
  {
    std::vector<FileMetaData*> level_files[config::kNumLevels];
  };

  struct mem_partition_guard
  {
    mem_partition_guard()
      :written_kvs(0),
      partition_mem(nullptr),
      partition_data(nullptr) {}

    mem_partition_guard(const Slice& start1, const Slice& end1)
      : partition_start_str(start1.ToString()),
        partition_end_str(end1.ToString()),
        written_kvs(0),
        partition_mem(nullptr),
        partition_data(nullptr) {
          partition_start= Slice(partition_start_str);
          partition_end = Slice(partition_end_str);   
      }

    // mem_partition_guard(const std::string& start1, const std::string& end1)
    //   : partition_start_str(start1),
    //     partition_end_str(end1),
    //     partition_start(partition_start_str.data(), partition_start_str.size()),
    //     partition_end(partition_end_str.data(), partition_end_str.size()),
    //     written_kvs(0),
    //     partition_mem(nullptr),
    //     partition_data(nullptr) {
    //        fprintf(stderr, "partition_start_str: %s\n", partition_start_str.c_str());
    //         fprintf(stderr, "partition_end_str: %s\n", partition_end_str.c_str());
    //         fprintf(stderr, "partition_start: %ld %s\n", partition_start.size(), partition_start.data());
    //         fprintf(stderr, "partition_end: %ld %s\n", partition_end.size(), partition_end.data());
    //     }
    
    ~mem_partition_guard() {
      if (partition_mem != nullptr) {
        partition_mem->Unref();  // Decrease reference count to manage memory
      }
    }


    bool contains(const std::string& value) const {
      return partition_start.compare(value) < 0 && partition_end.compare(value) > 0;
    }

    inline int CompareWithEnd(const char* key_ptr, size_t key_size) const {

      const size_t min_len = (partition_end.size() < key_size) ? partition_end.size() : key_size;
      int r = memcmp(partition_end.data(), key_ptr, min_len);
      if (r == 0) {
        if (partition_end.size() < key_size)
          r = -1;
        else if (partition_end.size() > key_size)
          r = +1;
      }
        return r;
    }


    Slice partition_start;
    Slice partition_end;
    std::string partition_start_str;
    std::string partition_end_str;

    int64_t written_kvs;

    MemTable* partition_mem;  

    partition_leveling_data *partition_data; 
  };


  struct PartitionGuardComparator {
    bool operator() (const mem_partition_guard* lhs, const mem_partition_guard* rhs) {
        return lhs->partition_start.compare(rhs->partition_start) < 0;
    }
  };

} // namespace leveldb


#endif // STORAGE_LEVELDB_INCLUDE_SLICE_H_
