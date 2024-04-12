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

void range_identifier::check_may_merge_split_range(){
    assert(temp_container.size() != 0);
    auto it = temp_container.begin();
    
    while (it != temp_container.end()) {
        auto start_it = it; // 当前范围的起始迭代器
        std::string start = *start_it; // 起始键
        std::string end = start; // 结束键，初始化为起始键
        uint64_t kv_num = keys[start]; // 初始化为起始键的出现次数

        auto next_it = std::next(it); // 下一个键的迭代器

        while (next_it != temp_container.end()) {
            long long curr = std::stoll(*it);
            long long next = std::stoll(*next_it);
            
            if (next - curr <= init_range_length) {
                end = *next_it; // 更新范围的结束键
                kv_num += keys[*next_it]; // 累加当前键的出现次数
                it = next_it;
                next_it = std::next(it);
            } else {
                break; // 当前键与下一个键之间的差值超出了初始范围长度，结束当前范围的处理
            }
        }

        // 添加区间到ranges，包括起始键、结束键和键的出现次数之和
        hot_ranges.insert(hotdb_range(start, end, kv_num));
        fprintf(stdout, "Inserted hot range - Start: %s, End: %s, KV Num: %lu\n", start.c_str(), end.c_str(), kv_num);


        if (next_it != temp_container.end()) {
            it = next_it; // 移动到下一个范围的起始键
        } else {
            break; // 已处理完所有元素
        }      
    }

    temp_container.clear();
    keys.clear();
}

void range_identifier::record_cold_ranges() {
    // 假设已定义一个用于解析范围的辅助函数，此处略过实现
    // 用于记录上一个处理的键
    std::string last_key_processed = "";
    uint64_t kv_num = 0;
    for (const auto& pair : keys) {
        std::string current_key = pair.first;
        if (temp_container.find(current_key) == temp_container.end()) { // 如果当前键不在热数据容器中
            if (last_key_processed.empty() || std::stoll(current_key) - std::stoll(last_key_processed) > 1) {
                // 如果是新的范围，记录上一个范围
                if (!last_key_processed.empty()) {
                    total_cold_ranges.insert(hotdb_range(last_key_processed, last_key_processed, kv_num));
                }
                kv_num = pair.second; // 重置当前范围的键出现次数
            } else {
                // 如果当前键与上一个键连续，累加出现次数
                kv_num += pair.second;
            }
            last_key_processed = current_key;
        }
    }
    // 记录最后一个范围
    if (!last_key_processed.empty()) {
        total_cold_ranges.insert(hotdb_range(last_key_processed, last_key_processed, kv_num));
    }
}


void range_identifier::record_cold_ranges(uint64_t cold_keys_count ) {

    std::vector<hotdb_range> new_ranges;
// 计算冷数据范围总和
    for (const auto& curr_range : current_cold_ranges) {
        bool merged = false;

        for (auto it = total_cold_ranges.begin(); it != total_cold_ranges.end();) {
            if (it->has_intersection_with(curr_range)) {
                // Calculate new range details based on intersection
                std::string new_start = std::min(it->start_str, curr_range.start_str);
                std::string new_end = std::max(it->end_str, curr_range.end_str);
                uint64_t new_kv_num = it->kv_number + curr_range.kv_number; // Simplified

                // Remove the old range and prepare to add a new merged range
                total_cold_ranges.erase(it++);
                new_ranges.push_back({new_start, new_end, new_kv_num});
                merged = true;
            } else {
                ++it;
            }
        }

        // If not merged, add the current range as a new range
        if (!merged) {
            new_ranges.push_back(curr_range);
        }
    }

    // Insert all new and merged ranges back into the set
    for (const auto& range : new_ranges) {
        total_cold_ranges.insert(range);
    }
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
        check_may_merge_split_range();
    }
    else{
        keys.clear();
    }

    record_cold_ranges(); // 记录冷数据范围
    
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
