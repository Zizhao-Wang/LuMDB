// Copyright (c) 2024: Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_
#define STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_

#include <string>
#include <set>
#include <vector>

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
      start = Slice(s);
      end = Slice(e);
      if(ran_len != -1){
        range_length = ran_len;
      }
      else{
        range_length = std::stoull(end.ToString()) - std::stoull(start.ToString());
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
        range_length = std::stoull(end.ToString()) - std::stoull(start.ToString());
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








}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_