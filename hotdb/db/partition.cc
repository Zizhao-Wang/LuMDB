#include "partition.h"

namespace leveldb {

  mem_partition_guard::mem_partition_guard()
    :written_kvs(0),
    total_file_size(0),
    partition_num(0),
    total_files(0),
    min_file_size(0),
    is_true_end(true) {}

  mem_partition_guard::mem_partition_guard(const Slice& start1, const Slice& end1)
      :partition_start_str(start1.ToString()),
      partition_end_str(end1.ToString()),
      written_kvs(0),
      total_file_size(0),
      partition_num(0),
      total_files(0),
      min_file_size(0),
      is_true_end(true) {
        partition_start= Slice(partition_start_str);
        partition_end = Slice(partition_end_str);   
      }

  mem_partition_guard::mem_partition_guard(const std::string& start1, const std::string& end1)
      :partition_start_str(start1),
      partition_end_str(end1),
      total_file_size(0),
      written_kvs(0),
      total_files(0),
      min_file_size(0),
      is_true_end(true) {
        partition_start= Slice(partition_start_str);
        partition_end = Slice(partition_end_str);   
      }
        
  mem_partition_guard::~mem_partition_guard() {}


  void mem_partition_guard::UpdatePartitionEnd(const std::string& new_end_str) {
    partition_end_str = new_end_str;
    partition_end = Slice(partition_end_str);
    // fprintf(stderr, "Partition end updated to %s\n", partition_end_str.c_str());
  }

  void mem_partition_guard::UpdatePartitionStart(const std::string& new_start_str) {
    partition_start_str = new_start_str;
    partition_start = Slice(partition_start_str);
  }

  unsigned long long mem_partition_guard::GetPartitionSize() const {
    return total_file_size;
  }

  unsigned long long mem_partition_guard::GetPartitionLength() const {
    return std::stoull(partition_end_str) - std::stoull(partition_start_str);
  }

  unsigned long long mem_partition_guard::GetPartitionStart() const {
    return std::stoull(partition_start_str);
  }

  unsigned long long mem_partition_guard::GetPartitionEnd() const {
    return std::stoull(partition_end_str);
  }

  void mem_partition_guard::Add_File(uint64_t file_size, uint64_t kvs){
    total_files++;
    total_file_size += file_size;
    if(min_file_size ==0 || file_size < min_file_size){
      min_file_size = file_size;
    }
    written_kvs += kvs;
  }

  uint64_t mem_partition_guard::GetMinFileSize() const {
    return min_file_size;
  }

  uint64_t mem_partition_guard::GetAverageFileSize() const {
    if(total_files == 0){
      return 0;
    }
    return total_file_size / total_files;
  }

  uint64_t mem_partition_guard::GetTotalFiles() const{
    return total_files;
  }

}// namespace leveldb