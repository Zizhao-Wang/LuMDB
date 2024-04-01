// Copyright (c) 2024: Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_
#define STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_

#include <string>
#include <set>
#include <vector>
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

struct hot_range{

  Slice start, end;
  uint64_t kv_number;
  int64_t range_length;
  std::string start_str, end_str;

  hot_range(const Slice& s, const Slice& e)
  :kv_number(0),
  start_str(s.ToString()),
  end_str(e.ToString()),
  start(Slice(start_str)),
  end((end_str)){
    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
  }

  hot_range(const std::string& s, const std::string& e)
  :kv_number(0),
  start_str(s),
  end_str(e),
  start(Slice(start_str)),
  end(Slice(end_str)){
    range_length = std::stoull(end.ToString()) - std::stoull(start.ToString()) + 1;
  }

  bool operator<(const hot_range& other) const {
    if (start.compare(other.start) < 0) return true;
    if (start.compare(other.start) == 0) return end.compare(other.end) < 0;
    return false;
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

  bool operator<(const hot_range& other) const {
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


class range_identifier
{

private:
  /* data */
  std::set<hot_range> hot_ranges;
  std::vector<batch_data> existing_batch_data;
  std::unordered_map<std::string, int32_t> keys;
  std::set<std::string, CompareStringLength> temp_container;
  uint64_t total_number, batch_size;
  int init_range_length, hot_definition;
  double bathc_hot_definition;

public:
  range_identifier(int init_length, int batch_length, double test_hot_denfinition=2);

  ~range_identifier();

  void print_ranges() const;

  void check_may_merge_range();

  void check_and_statistic_ranges();

  void add_data(const leveldb::Slice& data);

};




}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_