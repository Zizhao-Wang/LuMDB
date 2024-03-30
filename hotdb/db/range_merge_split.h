// Copyright (c) 2024: Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_
#define STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_

#include <string>
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
  bool splittable;
  uint64_t kv_number;
  uint64_t range_length;

  dynamic_range(const leveldb::Slice& s, const leveldb::Slice& e, bool split = true)
    : start(s), end(e), splittable(split) {
      // std::string str(key.data(), key.size());
      // uint64_t key_number = std::stoull(str);
      range_length = std::stoull(end.ToString()) - std::stoull(start.ToString());
    }
  
  dynamic_range(const std::string& s, const std::string& e, bool split = true)
    : start(s),
    end(e), 
    splittable(split),
    kv_number(0){
      range_length = std::stoull(end.ToString()) - std::stoull(start.ToString());
    }
  
  // bool is_split();

  int is_contains(const leveldb::Slice& value);

  void update(const leveldb::Slice& value);

};

class range_maintainer
{

private:
  /* data */
  std::vector<dynamic_range> ranges;
  uint64_t total_number;
  std::string temp_data;
  int init_range_length;

public:
  range_maintainer(int );

  void increase_number();

  ~range_maintainer();

  void add_data(const leveldb::Slice& data);

};








}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_