// Copyright (c) 2011 Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.



#ifndef STORAGE_LEVELDB_DB_PARTITION_H_
#define STORAGE_LEVELDB_DB_PARTITION_H_

#include <set>

#include "memtable.h"
#include "db/dbformat.h"
#include "leveldb/slice.h"
#include "db/version_set.h"
#include "db/version_edit.h"


namespace leveldb {

  struct mem_partition_guard;
  struct PartitionGuardComparator;




  struct mem_partition_guard
  {
    mem_partition_guard();

    mem_partition_guard(const Slice& start1, const Slice& end1);

    mem_partition_guard(const std::string& start1, const std::string& end1);
    
    ~mem_partition_guard();

    void UpdatePartitionEnd(const std::string& new_end_str);

    void UpdatePartitionStart(const std::string& new_start_str);
    inline bool contains(const std::string& value) const;
    inline bool contains(const char* value, size_t value_size) const;
    inline int CompareWithEnd(const char* key_ptr, size_t key_size) const;
    inline int CompareWithBegin(const char* key_ptr, size_t key_size) const;

    unsigned long long GetPartitionSize() const;
    unsigned long long GetPartitionLength() const;
    unsigned long long GetPartitionStart() const;
    unsigned long long GetPartitionEnd() const;

    void Add_File(uint64_t file_size, uint64_t kvs);
    uint64_t GetMinFileSize() const;
    uint64_t GetAverageFileSize() const;
    uint64_t GetTotalFiles() const;


    Slice partition_start;
    Slice partition_end;
    std::string partition_start_str;
    std::string partition_end_str;
    uint64_t written_kvs, total_file_size;
    uint64_t partition_num, total_files;
    uint64_t min_file_size;
    bool is_true_end;

    struct PartitionGuardComparator2 {
      bool operator()(const mem_partition_guard* lhs, const mem_partition_guard* rhs) const {
        return lhs->partition_start.compare(rhs->partition_start) < 0;
      }
    };

    std::set<mem_partition_guard*, PartitionGuardComparator2> sub_partitions_;
  };


  inline bool mem_partition_guard::contains(const std::string& value) const {
    return partition_start.compare(value) < 0 && partition_end.compare(value) > 0;
  }

  inline bool mem_partition_guard::contains(const char* value, size_t value_size) const {
    size_t start_size = partition_start.size();
    int cmp_result;
    size_t min_len_start = std::min(start_size, value_size);
    cmp_result = memcmp(partition_start.data(), value, min_len_start);
    if (cmp_result > 0 || (cmp_result == 0 && start_size < value_size)) {
      return false;
    }

    size_t end_size = partition_end.size();
    size_t min_len_end = std::min(end_size, value_size);
    cmp_result = memcmp(partition_end.data(), value, min_len_end);
    if (cmp_result < 0 || (cmp_result == 0 && end_size < value_size)) {
      return false;
    }

    return true;
  }

  inline int mem_partition_guard::CompareWithEnd(const char* key_ptr, size_t key_size) const {
    const size_t min_len = (partition_end.size() < key_size) ? partition_end.size() : key_size;
    int r = memcmp(partition_end.data(), key_ptr, min_len);
    if (r == 0) {
      if (partition_end.size() < key_size)
      r = -1;
      else if (partition_end.size() > key_size)
      r = +1;
    }
    if(!is_true_end && r==0){
      r = -1;
    }
    return r;
  }

  inline int mem_partition_guard::CompareWithBegin(const char* key_ptr, size_t key_size) const {
    const size_t min_len = (partition_start.size() < key_size) ? partition_start.size() : key_size;
    int r = memcmp(partition_start.data(), key_ptr, min_len);
    if (r == 0) {
      if (partition_start.size() < key_size)
        r = -1;
      else if (partition_start.size() > key_size)
        r = +1;
    }
    return r;
  }

  struct PartitionGuardComparator {
    bool operator() (const mem_partition_guard* lhs, const mem_partition_guard* rhs) const {
      return lhs->partition_start.compare(rhs->partition_start) < 0;
    }
  };

  inline bool IsLastPartition(const std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions_,  mem_partition_guard* current_partition) {
    auto it = mem_partitions_.find(current_partition);
    if (it != mem_partitions_.end()) {
      auto next_it = std::next(it);
      return next_it == mem_partitions_.end();
    }
    return false;
  }

  inline bool CanIncreasePartition(const std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions_,  mem_partition_guard* current_partition, 
    mem_partition_guard* next_partition, const char* key, size_t key_size) {

    if(next_partition == nullptr) {
      return false;
    }

    if(next_partition->CompareWithBegin(key, key_size)>0 && current_partition->GetTotalFiles() == 0) {
      return true;
    }

    return false;
  }


  inline bool CanIncreasePartitionStart(const std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions_,  mem_partition_guard* current_partition, 
    mem_partition_guard* next_partition, const char* key, size_t key_size) {

    if(next_partition == nullptr) {
      return false;
    }

    if(next_partition->CompareWithEnd(key, key_size)>0 && current_partition->GetTotalFiles() == 0) {
      return true;
    }

    return false;
  }


  inline void GetNextPartition(const std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions, mem_partition_guard* current_partition, mem_partition_guard*& next_partition, bool debug = false) {
    auto it = mem_partitions.upper_bound(current_partition);

    if (it != mem_partitions.begin()) {
      auto prev_it = std::prev(it);
      if ((*prev_it)->partition_start == current_partition->partition_start &&
        (*prev_it)->partition_end == current_partition->partition_end) {
        if (it != mem_partitions.end()) {
          next_partition = *it;
        } else {
          next_partition = nullptr;
        }
          return;
      } else {
        if (debug) {
          fprintf(stderr, "Fatal: Previous partition does not match current partition.\n");
          exit(0);
        }
      }
    } else {
      if (debug) {
        fprintf(stderr, "Fatal: Iterator is at the beginning of the set.\n");
        exit(0);
      }
    }
    next_partition = nullptr;
  }


  inline void RemovePartition(std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions, mem_partition_guard* current_partition) {
    auto it = mem_partitions.upper_bound(current_partition);

    if (it != mem_partitions.begin()) {
        auto prev_it = std::prev(it);
        if ((*prev_it)->partition_start_str == current_partition->partition_start_str &&
            (*prev_it)->partition_end_str == current_partition->partition_end_str) {
            delete *prev_it;
            mem_partitions.erase(prev_it);
        }
    }
  }


  inline mem_partition_guard* CreateAndInsertPartition(const std::string& start_key, const std::string& end_key, uint64_t new_allocated_partition,  std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions) {
    
    mem_partition_guard* new_partition = new mem_partition_guard(start_key, end_key);
    new_partition->partition_num = new_allocated_partition;
    mem_partitions.insert(new_partition);
    return new_partition;

  }


} // namespace leveldb


#endif // STORAGE_LEVELDB_INCLUDE_SLICE_H_
