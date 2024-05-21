// Copyright (c) 2024: Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_
#define STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_

#include <string>
#include <set>
#include <vector>
#include <exception>
#include <unordered_map>

#include "db/dbformat.h"
#include "leveldb/options.h"
#include "leveldb/db.h"

namespace leveldb {

class range_maintainer;
class dynamic_range;

class dynamic_range {
public:
  Slice start, end;
  std::string start_str, end_str;
  bool splittable;
  uint64_t kv_number;
  int64_t range_length;

  dynamic_range(const leveldb::Slice& s, const leveldb::Slice& e, bool split=true, int ran_len=-1)
    : splittable(split),kv_number(0) {
      start_str = s.ToString();
      end_str = e.ToString();
      start = Slice(start_str);
      end = Slice(end_str);
      if(ran_len != -1){
        range_length = ran_len;
      }
      else{
        range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
      }
    }
  
  dynamic_range(const std::string s, const std::string e, bool split=true, int ran_len=-1)
    : splittable(split),
    kv_number(0){
      start_str = s;
      end_str = e;
      start = Slice(start_str);
      end = Slice(end_str);
      if(ran_len != -1){
        range_length = ran_len;
      }
      else{
        range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1 ;
      }
    }
  
  // bool is_split();

  int is_contains(const leveldb::Slice& value) const;

  void update(const leveldb::Slice& value);

  bool operator<(const dynamic_range& other) const {
    if (start.compare(other.start) < 0) return true;
    if (start.compare(other.start) == 0) return end.compare(other.end) < 0;
    return false;
  }

};

class range_maintainer
{

private:
  /* data */
  std::set<dynamic_range> ranges;
  uint64_t total_number;
  Slice temp_data;
  char key_data[100];
  int init_range_length;

  uint64_t test;

public:
  range_maintainer(int );

  void increase_number();

  ~range_maintainer();

  void add_data(const leveldb::Slice& data);


  void print_ranges() const;

};


struct data_maintainer{
  Slice key_data;
  int64_t frequency;
};

struct hotdb_range{

  Slice start, end;
  uint64_t kv_number;
  uint64_t range_length;
  std::string start_str, end_str;

  hotdb_range(const Slice& s, const Slice& e, uint64_t kv_num)
  :kv_number(kv_num),
  start_str(s.ToString()),
  end_str(e.ToString()){
    start= Slice(start_str);
    end = Slice(end_str);
    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
  }

  hotdb_range(const std::string& s, const std::string& e)
  :kv_number(0),
  start_str(s),
  end_str(e),
  start(Slice(start_str)),
  end(Slice(end_str)){
    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
  }

  hotdb_range(const std::string& s, const std::string& e, uint64_t kv_num)
  :kv_number(kv_num),
  start_str(s),
  end_str(e),
  start(Slice(start_str)),
  end(Slice(end_str)){
    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
  }

  hotdb_range(std::string& s,  std::string& e, uint64_t kv_num)
  :kv_number(kv_num),start_str(s),end_str(e){
    start= Slice(start_str);
    end = Slice(end_str);
    // Outputting the initial state of the start and end slices
    // Output the initial state of the start and end slices
    // fprintf(stderr, "Start Slice: %s\n", start.ToString().c_str());
    // fprintf(stderr, "End Slice: %s\n", end.ToString().c_str());

    // Outputting addresses and sizes
    // fprintf(stderr, "Address of s: %p, size: %zu\n", &s, s.size());
    // fprintf(stderr, "Address of e: %p, size: %zu\n", &e, e.size());
    // fprintf(stderr, "Address of start_str: %p, size: %zu\n", (void*)start_str.data(), start_str.size());
    // fprintf(stderr, "Address of end_str: %p, size: %zu\n", (void*)end_str.data(), end_str.size());
    // fprintf(stderr, "Start data address: %p, actual pointed-to address: %p, size: %zu\n", (void*)start.data(), start.data(), start.size());
    // fprintf(stderr, "End data address: %p, actual pointed-to address: %p, size: %zu\n", (void*)end.data(), end.data(), end.size());

    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
    // fprintf(stderr,"start_str: %s\n", std::stoull(end.ToString()));
    // fprintf(stderr, "start_str:%llu, end:%llu, Range Length: %lu\n",std::stoull(end.ToString()),std::stoull(start.ToString()), range_length);
    // exit(0);
  }

  bool contains(const std::string& value) const {
    return start.compare(value) <= 0 && end.compare(value) >= 0;
  }

  double get_average_num_rate() const {
    return (double)kv_number / range_length;
  }

  bool should_merge(const hotdb_range& rhs){
    if (this->has_intersection_with(rhs)) {
      return true;
    }
    // 检查是否相邻
    if (std::stoull(this->end_str) + 1 == std::stoull(rhs.start_str) || std::stoull(rhs.end_str) + 1 == std::stoull(this->start_str)) {
      return true;
    }
    return false;
  }

  bool operator<(const hotdb_range& other) const {
    if (start.compare(other.start) < 0) return true;
    if (start.compare(other.start) == 0) return end.compare(other.end) < 0;
    return false;
  }

  uint64_t length() const {
    return range_length;
  }

  // bool has_intersection(const hotdb_range& other) const {
  //   return !(end_str < other.start_str || start_str > other.end_str);
  // }

  bool has_intersection_with(const hotdb_range& rhs) const {
    return start.compare(rhs.end) <= 0 && end.compare(rhs.start) >= 0;
  } 

  void merge_with(const hotdb_range& other) {
    // 更新start_str和end_str为合并后的范围边界
    start_str = std::min(start_str, other.start_str);
    end_str = std::max(end_str, other.end_str);
    // 假设键值数量是平均分布的，重新计算kv_number
    uint64_t total_length = std::stoull(end_str) - std::stoull(start_str) + 1;
    kv_number = (kv_number * range_length + other.kv_number * other.range_length) / total_length;
    range_length = total_length; // 更新范围长度
  }

  void print_range_info(){
    fprintf(stdout, " [Range Information] start: %llu , end: %llu, length: %lu kv_num:%lu\n", std::stoull(start_str), std::stoull(end_str), range_length, kv_number);
  }

};


struct batch_data{

  Slice start, end;
  std::string start_str, end_str;
  double fre_per_number;


  batch_data(const std::string& s, const std::string& e, uint64_t batch_number, uint64_t batch_length)
  :start_str(s),
  end_str(e),
  start(Slice(start_str)),
  end(Slice(end_str)),
  fre_per_number((double)batch_number/batch_length){}

  bool operator<(const hotdb_range& other) const {
    if (start.compare(other.start) < 0) return true;
    if (start.compare(other.start) == 0) return end.compare(other.end) < 0;
    return false;
  }

};


struct CompareStringLength {
  bool operator()(const std::string& a, const std::string& b) const {
    const size_t min_len = (a.size() < b.size()) ? a.size() : b.size();
    int r = memcmp(a.data(), b.data(), min_len);
    if (r < 0) return true;  // 如果a在字典序上小于b，则a < b
    if (r > 0) return false; // 如果a在字典序上大于b，则a > b
    return a.size() < b.size();
  }
};


struct KeyCompare {
  bool operator()(const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) const {
    if (a.second == b.second) {
      return a.first < b.first; // 如果second相同，则按first排序
    }
    return a.second < b.second; // 主要按second排序
  }
};

class range_identifier
{

private:
  /* data */
  int test_num;
  bool is_first;
  std::vector<hotdb_range> hot_ranges;
  std::vector<hotdb_range> current_ranges;

  std::set<hotdb_range> total_cold_ranges;
  std::set<hotdb_range> current_cold_ranges;

  std::vector<batch_data> existing_batch_data;

  /**
   * @brief record key-frequency pairs using map
   */
  std::unordered_map<std::string, int32_t> keys;
  std::vector<std::pair<std::string, int32_t>> sorted_keys;
  std::set<std::string> sorted_by_keys;


  std::vector<std::pair<std::string, int32_t>> keys_data;
  std::set<std::pair<std::string, int>, KeyCompare> keys_data_set;

  std::set<std::string, CompareStringLength> temp_container;
  uint64_t total_number, batch_size;
  int init_range_length, hot_definition;
  double bathc_hot_definition;

public:
  range_identifier(int init_length, int batch_length, double test_hot_denfinition=2);



  ~range_identifier();

  void print_hot_ranges() const;

  void print_currenthot_ranges() const;

  void print_temp_container() const;

  void merge_ranges_in_container(bool is_first);

  void check_may_merge_split_range();

  // 合并两个区间
  hotdb_range merge_ranges( hotdb_range& first, const hotdb_range& second); 

  void may_merge_internal_ranges_in_total_range();

  void may_merge_split_range_to_total_range();

  void record_cold_ranges();

  void record_cold_ranges(uint64_t hot_keys_total_count);

  void check_and_statistic_ranges();


  void printMap(const std::unordered_map<std::string, int32_t>& map);



  void vector_sort_merge_data();

  void unordered_map_sort_merge_data();



  void set_increment(std::string& key);

  void vector_increment(std::string& key);

  void unordered_map_increment(std::string& key);



  void add_data(const leveldb::Slice& data);


  // 
  bool is_hot(const Slice& key) const;

};




}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_