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

int dynamic_range::is_contains(const leveldb::Slice& value) const {
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
    init_range_length(init_range),
    test(0){
        memset(key_data, 0, sizeof(key_data));
        temp_data = Slice(key_data, sizeof(key_data));
    }

range_maintainer::~range_maintainer()
{
    std::fprintf(stdout,"There %zu ranges was released in hot ranges test:%lu!\n",ranges.size(),test);
}

inline void range_maintainer::increase_number(){
    total_number++;
}

void range_maintainer::add_data(const leveldb::Slice& data){

    if(total_number == 0){
        std::string temp_data_str = data.ToString();
        ranges.emplace(temp_data_str, temp_data_str,true,0); // Initialize the ranges set with the first range
        total_number++; 
        return ;
    }

    // Check if new data can be grouped into an existing Range
    std::string data_str = data.ToString();
    auto it = ranges.lower_bound(dynamic_range(data_str, data_str));

    if (it != ranges.end() && it->is_contains(data)) {
        dynamic_range currentRange = *it; // Make a copy to modify
        if (currentRange.range_length > init_range_length) {
            currentRange.end_str = data_str;
            currentRange.end.clear();
            currentRange.end = Slice(currentRange.end_str);
            total_number++;
        }
        else{
            currentRange.kv_number++;
        }
    }
    else{

        bool expanded = false;
        if (it != ranges.begin()) {
            auto prev_it = std::prev(it);
            if (std::stoull(prev_it->end.ToString()) + 1 == std::stoull(data.ToString())) {
                // 更新前一个区间
                dynamic_range updated_range = *prev_it;
                ranges.erase(prev_it);
                updated_range.end_str = data.ToString();
                updated_range.end = Slice(updated_range.end_str);
                updated_range.range_length = std::stoull(updated_range.end.ToString()) - std::stoull(updated_range.start.ToString()) + 1;
                ranges.insert(updated_range);
                expanded = true;
            }
        }

        // 如果没有向前扩张，并且存在紧邻的后区间，尝试合并
        if (!expanded && it != ranges.end() && std::stoull(data.ToString()) + 1 == std::stoull(it->start.ToString())) {
            // 更新后一个区间
            dynamic_range updated_range = *it;
            ranges.erase(it);
            updated_range.start_str = data.ToString();
            updated_range.start = Slice(updated_range.start_str);
            updated_range.range_length = std::stoull(updated_range.end.ToString()) - std::stoull(updated_range.start.ToString()) + 1;
            ranges.insert(updated_range);
            expanded = true;
        }

        // 如果未能扩张任何区间，则创建一个新区间
        if (!expanded) {
            ranges.emplace(data.ToString(), data.ToString());
            total_number++;
        }
    }

    // if(!temp_data.empty()){
        
    //     std::fprintf(stdout, "start:%llu [start addr:%p] end:%llu [end addr:%p] \n",
    //          std::stoull(temp_data.ToString()),
    //          static_cast<const void*>(temp_data.data()), 
    //          std::stoull(data.ToString()), 
    //          static_cast<const void*>(data.data())); 

    //     if(temp_data.compare(data) <= 0) {
    //         // temp_data小于或等于data，正常顺序
    //         ranges.emplace(temp_data, data.ToString(), true);
    //     } else {
    //         // temp_data大于data，需要反转顺序
    //         ranges.emplace(data, temp_data.ToString(), true);
    //     }

        // std::fprintf(stdout, "Range(start:%llu [start_addr:%p]; end:%llu [end_addr:%p]; length:%ld) has been created!  \n",
        //      std::stoull(ranges[0].start.ToString()), 
        //      static_cast<const void*>(ranges[0].start.data()),
        //      std::stoull(ranges[0].end.ToString()), 
        //      static_cast<const void*>(ranges[0].end.data()),
        //      ranges[0].range_length);

        // return ;
    // }

    total_number++;
    return ;
}


void range_maintainer::print_ranges() const {
    for (const auto& range : ranges) {
        unsigned long long start = std::stoull(range.start.ToString());
        unsigned long long end = std::stoull(range.end.ToString());
        fprintf(stdout, "Range start: %llu, end: %llu, length: %lu\n", start, end, range.range_length);
    }
}



range_identifier::range_identifier(int init_length, int batch_length,double test_hot_denfinition)
    :init_range_length(init_length),
    total_number(0),
    batch_size(batch_length),
    hot_definition(10),
    bathc_hot_definition(test_hot_denfinition){} // the default hot_definition is 10%!

range_identifier::~range_identifier(){
    std::fprintf(stdout,"There %zu ranges was released in hot ranges!\n",hot_ranges.size());
}


void range_identifier::print_ranges() const{
    for (const auto& range : hot_ranges) {
        unsigned long long start = std::stoull(range.start.ToString());
        unsigned long long end = std::stoull(range.end.ToString());
        fprintf(stdout, "Range start: %llu, end: %llu, length: %lu\n", start, end, range.range_length);
    }
}

void range_identifier::check_may_merge_range(){
    assert(temp_container.size()!=0);

    auto it = temp_container.begin();
    while (it != temp_container.end()) {
        auto start_it = it;
        auto next_it = std::next(it);
        std::string start = *start_it;
        std::string end = start;

        while (next_it != temp_container.end()) {
            long long curr = std::stoll(*it);
            long long next = std::stoll(*next_it);
            if (next - curr <= init_range_length) {
                end = *next_it;
                it = next_it;
                next_it = std::next(it);
            } else {
                break;    
            }

            // 添加区间到ranges
            hot_ranges.insert(hot_range(start, end));

            if (next_it != temp_container.end()) {
                it = next_it;
            } else {
                break; // 已处理完所有元素
            }
        }       
    }

    temp_container.clear();
    keys.clear();
}

void range_identifier::check_and_statistic_ranges(){
    int hot_definition_in_batch = hot_definition * bathc_hot_definition;
    int hot_frequency = batch_size * hot_definition_in_batch / 100;
    for (const auto& pair : keys) {
        if(pair.second >= hot_frequency){
            temp_container.emplace(pair.first);
        }
    }

    if(!temp_container.empty()){
        check_may_merge_range();
    }+
    else{
        keys.clear();
    }

    if(!temp_container.empty()){
        std::fprintf(stdout, "Found %zu hot data in this batch.\n", temp_container.size());
        check_may_merge_range();
    } 
    
//     else {
//         std::fprintf(stdout, "No hot data found in this batch.\n");
//     }
}

void range_identifier::add_data(const leveldb::Slice& data){
    std::string key = data.ToString();

    auto it = keys.find(key);
    if (it != keys.end()) {
        // if found
        it->second += 1;
    } else {
        // if not found
        keys[key] = 1;
    }

    total_number++;

    if(total_number!=0 && total_number%batch_size == 0){
        check_and_statistic_ranges();
    }
}

}  // namespace leveldb
