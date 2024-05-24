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



#if 0
inline void range_identifier::set_increment(std::string& key) {
    auto it = keys_data_set.lower_bound({key, 0});  // 找到位置或可能插入的位置
    if (it != keys_data_set.end() && it->first == key) {
        // 如果找到，更新值
        int new_count = it->second + 1;
        keys_data_set.erase(it);  // 移除旧元素
        keys_data_set.insert({key, new_count});  // 插入更新后的新元素
    } else {
        // 如果未找到，插入新键值对
        keys_data_set.insert({key, 1});
    }
}

inline void range_identifier::vector_increment(std::string& key){
    keys_data.emplace_back(std::make_pair(key, 1));
}
#endif





range_identifier::range_identifier(int init_length, int batch_length,double test_hot_denfinition)
    :init_range_length(init_length),
    total_number(0),
    batch_size(batch_length),
    hot_definition(10),
    bathc_hot_definition(test_hot_denfinition),
    test_num(0),
    is_first(true)
    {
        sorted_keys.reserve(batch_length);  
    } // the default hot_definition is 10%!

range_identifier::~range_identifier(){
    std::fprintf(stdout,"There %zu ranges was released in hot ranges!\n",hot_ranges.size());
}


void range_identifier::print_hot_ranges() const{
    fprintf(stdout, "========Total hot Ranges start (%lu)========\n",hot_ranges.size());
    for (const auto& range : hot_ranges) {
        unsigned long long start = std::stoull(range.start_str);
        unsigned long long end = std::stoull(range.end_str);
        fprintf(stdout, "hot: Range start: %llu, end: %llu, length: %lu kv_num:%lu\n", start, end, range.range_length, range.kv_number);
    }
    fprintf(stdout, "========Total hot Ranges end ========\n");
}

void range_identifier::print_currenthot_ranges() const{
    fprintf(stdout, "========Current hot Ranges start(%lu) ========\n",current_ranges.size());
    for (const auto& range : current_ranges) {
        unsigned long long start = std::stoull(range.start_str);
        unsigned long long end = std::stoull(range.end_str);
        fprintf(stdout, "Range start: %llu, end: %llu, length: %lu kv_num:%lu\n", start, end, range.range_length, range.kv_number);
    }
    fprintf(stdout, "========Current hot Ranges end ========\n");
}


void range_identifier::print_temp_container() const {
    fprintf(stdout, "\n ========Contents of temp_container(%lu)========\n",temp_container.size());
    for (const std::string& str : temp_container) {
        fprintf(stdout, "%s\n", str.c_str());
    }
    fprintf(stdout, " ========Contents of temp_container======== \n\n");
}


void range_identifier::record_cold_ranges(uint64_t cold_keys_count ) {

    if(total_cold_ranges.size()==0){
        std::string start = sorted_keys[0].first;
        std::string end = start;
        uint64_t kv_num = sorted_keys[0].second;
        for (size_t i = 1; i < sorted_keys.size(); ++i) {
            const auto& pair = sorted_keys[i];
            long long curr_start = std::stoll(start);
            long long next_value = std::stoll(pair.first);
            if ((next_value - curr_start + 1) <= init_range_length) {
                end = pair.first;
                kv_num += pair.second;
            } else {
                total_cold_ranges.insert(hotdb_range(start, end, kv_num));
                start = pair.first;
                end = start;
                kv_num = pair.second;
            }
        }
        total_cold_ranges.insert(hotdb_range(start, end, kv_num));
    }else{

    }
}

hotdb_range range_identifier::merge_ranges(hotdb_range& first, const hotdb_range& second){
    CompareStringLength comp;
    first.start_str = first.start.compare(second.start) ? first.start_str : second.start_str;
    first.end_str = first.end.compare(second.end) ? second.end_str : first.end_str;

    first.kv_number += second.kv_number;  // 累加 kv_number

    // 更新 range_length
    first.range_length = std::stoull(first.end_str) - std::stoull(first.start_str) + 1;
    fprintf(stdout, "Updated hot range - Start: %s, End: %s, KV Num: %lu, range length:%ld\n",
        first.start.data(), first.end.data(), first.kv_number, first.range_length);


    return first;
}

// fprintf(stdout, "First range - Start: %s, End: %s, Average Num Rate: %.2f\n",
//     it->start.data(), it->end.data(), it->get_average_num_rate());
// fprintf(stdout, "Second range - Start: %s, End: %s, Average Num Rate: %.2f\n",
//     next_it->start.data(), next_it->end.data(), next_it->get_average_num_rate());
// if(it->has_intersection_with(*next_it)){
//     fprintf(stdout,"yes!\n");
// }

void range_identifier::may_merge_internal_ranges_in_total_range(){
    if (hot_ranges.empty()) return;
    /**
    * merging current total hot ranges
    **/
    auto it = hot_ranges.begin();
    while (it != hot_ranges.end()) {
        if (std::next(it) != hot_ranges.end()) {
            auto next_it = std::next(it);
            if (it->should_merge(*next_it) ) {  //&& (it->get_average_num_rate() - next_it->get_average_num_rate()) < 1.0
                merge_ranges(*it, *next_it);
                hot_ranges.erase(next_it);
                // fprintf(stdout, "=========split_range_to_total_range Merge: ========\n");
                // print_hot_ranges();
                continue;
            }
        }
        ++it;
    }
}


void range_identifier::may_merge_split_range_to_total_range(){

    /**
    * put current_ranges(current hot ranges) into total_hot_ranges and then check if need to merge
    **/
    auto insert_it = current_ranges.begin(); // 用于遍历current_ranges
    auto current_it = hot_ranges.begin(); // 指向hot_ranges的当前位置
    auto next_it = std::next(current_it); // 指向hot_ranges的下一个位置

    print_currenthot_ranges();

    while (insert_it != current_ranges.end()) {
        // 找到适当的插入位置
        while (next_it != hot_ranges.end() ) {
            if(current_it->end.compare(insert_it->start)<0 && next_it->start.compare(insert_it->end)>0){
                break;
            }
            ++current_it;
            ++next_it;
        }

        // 插入元素
        if (next_it != hot_ranges.end()) {
            current_it->print_range_info();
            next_it->print_range_info();
            hot_ranges.insert(next_it, *insert_it);
            // insert_it = current_ranges.erase(insert_it); 
        } else {
            fprintf(stdout, "Inserted range from current_ranges into hot_ranges in else - Start: %s, End: %s, KV Num: %lu\n",
            insert_it->start_str.c_str(), insert_it->end_str.c_str(), insert_it->kv_number);
            hot_ranges.insert(hot_ranges.end(), insert_it, current_ranges.end());
            break;
        }

        fprintf(stdout, "Inserted range from current_ranges into hot_ranges - Start: %s, End: %s, KV Num: %lu\n",
            insert_it->start_str.c_str(), insert_it->end_str.c_str(), insert_it->kv_number);

        // 移向current_ranges的下一个元素
        ++insert_it;

        // 准备下一个插入的位置判断
        if (next_it != hot_ranges.end()) {
            current_it = next_it;
            next_it = std::next(next_it);
        }
    }

    print_hot_ranges();

    may_merge_internal_ranges_in_total_range();

    print_hot_ranges();

    exit(0);

}

void range_identifier::merge_ranges_in_container(bool is_first) {

    std::vector<hotdb_range>& target_ranges = is_first ? hot_ranges : current_ranges;
    const char* target_name = is_first ? "hot_ranges" : "current_ranges";


    // print_temp_container();

    for (auto it = temp_container.begin(); it != temp_container.end();) {
        std::string start = *it;
        std::string end = start;
        uint64_t kv_num = keys.at(start);
        long long curr_value = std::stoll(start);

        auto next_it = std::next(it);
        while (next_it != temp_container.end()) {
            long long next_value = std::stoll(*next_it);
            // 检查数字是否连续
            if (next_value == curr_value + 1) {
                end = *next_it;  // 更新结束点
                kv_num += keys.at(end);  // 更新键值数量
                curr_value = next_value;  // 更新当前起始点
                it = next_it;  // 移动迭代器
                next_it = std::next(it);  // 预先移动next_it
            } else {
                break;  // 如果不连续，跳出循环
            }
        }

        fprintf(stdout, "Inserted hot range into %s - Start: %s, End: %s, KV Num: %lu range length:%lld\n",
                target_name, start.c_str(), end.c_str(), kv_num, std::stoll(end) - std::stoll(start) + 1);
        target_ranges.emplace_back(hotdb_range(start, end, kv_num));

        // 确保迭代器前进到下一个待处理元素
        it = std::next(it);
    }
}


void range_identifier::check_may_merge_split_range(){
    assert(temp_container.size() != 0);

    if(hot_ranges.size()==0){
        merge_ranges_in_container(true);
        may_merge_internal_ranges_in_total_range();
        // print_hot_ranges();
    }else{
        // print_temp_container();
        for (auto it = temp_container.begin(); it != temp_container.end(); ) {
            bool found = false;
            for (auto& range : hot_ranges) {
                if (range.contains(*it)) {
                    // if(*(it)=="0000000000000044"){
                    //     fprintf(stdout, "range num:%ld key_num:%d\n", range.kv_number,keys[*it] );
                    // }
                    range.kv_number += keys[*it]; 
                    // if(*(it)=="0000000000001248"){
                    //     fprintf(stdout, "range num:%ld key_num:%d\n", range.kv_number,keys[*it] );
                    // }
                    it = temp_container.erase(it); // 移除当前元素，迭代器自动更新到下一个元素
                    found = true;
                    break; 
                }
            }
            if (!found) {
                it = std::next(it); 
            }
        }

        // print_hot_ranges();
        // print_temp_container();
        merge_ranges_in_container(false);

        // print_currenthot_ranges();
        if(current_ranges.size() != 0){
            may_merge_split_range_to_total_range();
        }
    }

    // print_hot_ranges();
    // test_num++;
    // if(test_num==4){
    //     exit(0);
    // }
    
}

void range_identifier::check_and_statistic_ranges(){
    // int hot_definition_in_batch = hot_definition * bathc_hot_definition;
    // int hot_frequency = batch_size * hot_definition_in_batch / 100;

    int hot_keys_per_batch = batch_size * hot_definition / 100;
    int hot_frequency_per_key = bathc_hot_definition;

    for (int i = 0; i < hot_keys_per_batch && i < sorted_keys.size(); ++i) {
        const auto& pair = sorted_keys[i];
        if (pair.second >= hot_frequency_per_key) {
            temp_container.emplace(pair.first);
            // if(pair.first=="0000000000001248"){
            //     fprintf(stdout, "key:%s number:%d in check_and_statistic_ranges()\n", pair.first.c_str(),pair.second);
            // }
            // fprintf(stdout, "%s number:%d \n", pair.first.c_str(),pair.second);
        }
    }

    if(!temp_container.empty()){
        check_may_merge_split_range();
    }

    // record_cold_ranges(); // 记录冷数据范围
    
}

void range_identifier::printMap(const std::unordered_map<std::string, int32_t>& map) {
    for (const auto& pair : map) {
        fprintf(stderr, "Key: %s, Value: %d\n", pair.first.c_str(), pair.second);
    }
}

void range_identifier::vector_sort_merge_data(){
    // 排序和归并次数
    std::sort(keys_data.begin(), keys_data.end(), [](const std::pair<std::string, int32_t>& a, const std::pair<std::string, int32_t>& b) {
        return a.first< b.first;  // 按键排序，为归并做准备
    });

    // 归并次数
    std::vector<std::pair<std::string, int32_t>> merged;
    for (const auto& item : keys_data) {
        if (!merged.empty() && merged.back().first == item.first) {
            merged.back().second += 1;  // 同一个键，次数累加
        } else {
            merged.push_back(item);  // 不同键，添加新元素
        }
    }

    // 最后按次数排序
    std::sort(merged.begin(), merged.end(), [](const std::pair<std::string, int32_t>& a, const std::pair<std::string, int32_t>& b) {
        return a.second > b.second;  // 按次数降序排序
    });

    // fprintf(stderr, "Merged data size: %zu\n", merged.size());
}

void range_identifier::unordered_map_sort_merge_data(){

    for (const auto& kv : keys) {
        sorted_keys.emplace_back(kv.first, kv.second); // 将unordered_map的元素复制到vector
        sorted_by_keys.emplace(kv.first);
    }

    std::sort(sorted_keys.begin(), sorted_keys.end(),
        [](const std::pair<std::string, int32_t>& a, const std::pair<std::string, int32_t>& b) {
            return a.second > b.second; 
    });

    // fprintf(stderr, "Merged data size: %zu\n", sorted_keys.size());
}

inline void range_identifier::unordered_map_increment(std::string& key){
    auto it = keys.find(key);
    if (it != keys.end()) {
        // if found, increment the count
        it->second += 1;
    } else {
        // if not found, insert new key with count 1
        keys[key] = 1;
    }
}

void range_identifier::add_data(const leveldb::Slice& data){

    std::string key = data.ToString();
    // set_increment(key);
    // vector_increment(key);
    unordered_map_increment(key);
    total_number++;


    if(total_number!=0 && total_number%batch_size == 0){
        // vector_sort_merge_data();  // 1.
        // keys_data.clear();
        unordered_map_sort_merge_data(); // 2.
        check_and_statistic_ranges(); 
        keys.clear();
        sorted_keys.clear();
        sorted_by_keys.clear();
        temp_container.clear();
        current_ranges.clear();
        // set_sort_merge_data(); //3.
        // keys_data_set.clear();
    }
}


bool range_identifier::is_hot(const Slice& key) const {

    if(std::stoull(key.ToString())<10000){
        return true;
    }
    return false; 
}

}  // namespace leveldb
