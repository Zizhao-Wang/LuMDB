// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/range_merge_split.h"
#include "db/dbformat.h"


namespace leveldb {






// 检查区间是否可以基于目标长度进行分裂
bool dynamic_range::is_split() {
    return splittable && range_length > 10;
}

bool dynamic_range::is_contains(const leveldb::Slice& value) {
    return start.compare(value) <= 0 && end.compare(value) >= 0;
}

void dynamic_range::update(const leveldb::Slice& value) {
    if (start.compare(value) > 0) start = value;
    if (end.compare(value) < 0) end = value;
}





range_maintainer::range_maintainer()
    :total_number(0){}

range_maintainer::~range_maintainer()
{
}

inline void range_maintainer::increase_number(){
    total_number++;
}

void range_maintainer::add_data(const leveldb::Slice& data){
    
    if(total_number == 0){
        std::string data1 = data.ToString();
        temp_data = Slice(data1); 
        return ;
    }

    // Check if new data can be grouped into an existing Range
    for (auto& range : ranges) {
        if (range.is_contains(data)) {
            range.update(data);
            return;
        }
    }

    Slice range_bound = Slice(data.ToString());
    ranges.emplace_back(temp_data.ToString(), range_bound.ToString(), true);
    temp_data.clear();
}

}  // namespace leveldb
