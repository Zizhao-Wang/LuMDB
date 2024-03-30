// Copyright (c) 2024: Zizhao Wang. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_
#define STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_

#include <string>
#include <vector>

#include "db/dbformat.h"
#include "leveldb/db.h"

namespace leveldb {

class dynamic_range {

public:
  Slice start, end;
  bool splittable;
  uint64_t range_length;

  dynamic_range(const leveldb::Slice& s, const leveldb::Slice& e, bool split = true)
    : start(s), end(e), splittable(split) {
      // std::string str(key.data(), key.size());
      // uint64_t key_number = std::stoull(str);
      range_length = std::stoull(end.ToString()) - std::stoull(start.ToString());
    }
  
  dynamic_range(const std::string& s, const std::string& e, bool split = true)
    : start(s),end(e), splittable(split){}
  
  bool is_split();

  bool is_contains(const leveldb::Slice& value);

  void update(const leveldb::Slice& value);

};

class range_maintainer
{

private:
  /* data */
  std::vector<dynamic_range> ranges;
  uint64_t total_number;
  leveldb::Slice temp_data;

public:
  range_maintainer();

  void increase_number();

  ~range_maintainer();

  void add_data(const leveldb::Slice& data);

};








}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_RANGE_MERGE_SPLIT_H_