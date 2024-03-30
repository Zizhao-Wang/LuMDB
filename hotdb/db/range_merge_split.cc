// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/range_merge_split.h"
#include "db/dbformat.h"


namespace leveldb {


/**
 * 
 */
// bool dynamic_range::is_split() {
//     return splittable && range_length > 10;
// }

int dynamic_range::is_contains(const leveldb::Slice& value) {
    // if (start.compare(value) <= 0 && end.compare(value) >= 0){
    //     if(splittable && range_length > 10){
    //         return 1;
    //     }
    //     return 2;
    // }

    // return 0;

    return start.compare(value) <= 0 && end.compare(value) >= 0;
}

void dynamic_range::update(const leveldb::Slice& value) {
    if (start.compare(value) > 0) start = value;
    if (end.compare(value) < 0) end = value;
}


/**
 * 
 */
range_maintainer::range_maintainer(int init_range)
    :total_number(0),
    init_range_length(init_range){}

range_maintainer::~range_maintainer()
{
    if(!temp_data.empty()){
        temp_data.clear();
    }
}

inline void range_maintainer::increase_number(){
    total_number++;
}

void range_maintainer::add_data(const leveldb::Slice& data){
    
    total_number++;

    if(total_number == 0){
        std::string data1 = data.ToString();
        temp_data = Slice(data1); 
        return ;
    }

    // Check if new data can be grouped into an existing Range
    for (auto& range : ranges) {
        if (range.is_contains(data)) {
            if(range.splittable && range.range_length > init_range_length){
                temp_data = std::string(range.end.data(), range.end.size());
                // 更新当前Range的结束点为新插入的数据
                range.end.clear();
                range.end = Slice(data.ToString());
                // 将原始Range的结束数据点设为下一个待处理数据点
                return ;
            }
            range.kv_number++;
            return;
        }
    }

    if(!temp_data.empty()){
        ranges.emplace_back(temp_data.ToString(), data.ToString(), true);
        return ;
    }
    temp_data.clear();
}

}  // namespace leveldb
