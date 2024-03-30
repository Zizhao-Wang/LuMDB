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

    std::fprintf(stdout,"There %zu ranges was released in hot ranges!\n",ranges.size());
}

inline void range_maintainer::increase_number(){
    total_number++;
}

void range_maintainer::add_data(const leveldb::Slice& data){

    std::fprintf(stdout,"data:%llu!\n",std::stoull(data.ToString()));

    if(total_number == 0){
        temp_data = data.ToString();
        std::fprintf(stdout,"temp_data:%llu!\n",std::stoull(temp_data));
        // std::fprintf(stdout, "data address: %p\n", static_cast<const void*>(temp_data.data()));
        total_number++; 
        return ;
    }
    std::fprintf(stdout, "data address: %p\n", static_cast<const void*>(temp_data.data()));
    // if(!temp_data.empty()){
    //     std::fprintf(stdout,"temp_data:%llu!\n",std::stoull(temp_data.ToString()));
    // }
    // Check if new data can be grouped into an existing Range
    // if(ranges.size()!=0){
    //     std::fprintf(stdout,"enter:\n");
    //     for (auto& range : ranges) {
    //         if (range.is_contains(data)) {
    //             if(range.splittable && range.range_length > init_range_length){
    //                 temp_data = std::string(range.end.data(), range.end.size());
    //                 range.end.clear();
    //                 range.end = Slice(data.ToString());
    //                 total_number++;
    //             }else{
    //                 range.kv_number++;
    //                 return;
    //             }
    //         }
    //     }
    // }

    
    if(!temp_data.empty()){
        
        // std::fprintf(stdout, "1 range(start addr:%p  end addr:%p, length:) has been created!\n",
        //                     static_cast<const void*>(temp_data.data()),
        //                     static_cast<const void*>(data.data()));

        // std::fprintf(stdout,"1 range(start:%llu  end:%llu, length:0) has been created!\n",
        //                     std::stoull(temp_data), 
                            // std::stoull(data.ToString()));

        if(temp_data.compare(data) <= 0) {
            // temp_data小于或等于data，正常顺序
            ranges.emplace_back(temp_data, data.ToString(), true);
        } else {
            // temp_data大于data，需要反转顺序
            ranges.emplace_back(data, temp_data.ToString(), true);
        }

        // std::fprintf(stdout,"1 range(start:%llu  end:%llu, length:%ld) has been created!\n",
        //                     std::stoull(temp_data), 
        //                     std::stoull(data.ToString()), 
        //                     ranges[0].range_length);

        // std::fprintf(stdout, "1 range(start addr:%p  end addr:%p, length:%ld) has been created!\n",
        //                     static_cast<const void*>(temp_data.data()), 
        //                     static_cast<const void*>(data.data()), 
        //                     ranges[0].range_length);

        return ;
    }

    total_number++;
    temp_data.clear();
}

}  // namespace leveldb
