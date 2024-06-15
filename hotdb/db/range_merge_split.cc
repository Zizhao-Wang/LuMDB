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
    } 

range_identifier::~range_identifier(){
    std::fprintf(stdout,"There %zu ranges was released in hot ranges!\n",hot_ranges.size());
}


void range_identifier::print_hot_ranges() const {


    fprintf(stdout, "========Total hot Ranges start ========\n");

    // 遍历所有的范围
    int set_index = 0;
    for (const auto& pair : all_hot_ranges) {
        fprintf(stdout, "  ========Hot Range Set %d start========\n", set_index);
        for (const auto& range : pair.second) {
            unsigned long long start = std::stoull(range->start_str);
            unsigned long long end = std::stoull(range->end_str);
             fprintf(stdout, "    hot: Range start: %llu, end: %llu, length: %lu kv_num:%lu\n", 
                    start, end, range->range_length, range->kv_number);
        }
    }

    fprintf(stdout, "========Total hot Ranges end ========\n");
}


void range_identifier::print_currenthot_ranges() const{
    fprintf(stdout, "========Current hot Ranges start(%lu) ========\n",current_ranges.size());
    for (const auto& range : current_ranges) {
        unsigned long long start = std::stoull(range->start_str);
        unsigned long long end = std::stoull(range->end_str);
        fprintf(stdout, "Range start: %llu, end: %llu, length: %lu kv_num:%lu\n", start, end, range->range_length, range->kv_number);
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

void range_identifier::print_range_set(const std::set<hotdb_range*, SortByStartString>& range_set) const {
    fprintf(stdout, "======== Range Set Output Start(size: %lu) ========\n", range_set.size());
    for (const auto& range : range_set) {
        fprintf(stdout, "Range: start=%s, end=%s, length=%lu, kv_num=%lu\n",
                range->start_str.c_str(), range->end_str.c_str(),
                range->end_int - range->start_int + 1, range->kv_number);
    }
    fprintf(stdout, "======== Range Set Output End ========\n");
}

void range_identifier::print_total_ranges() const {
    size_t total_ranges = 0;
    for (const auto& pair : all_hot_ranges) {
        total_ranges += pair.second.size();
    }
    fprintf(stdout, "Total ranges after merging: %lu\n", total_ranges);
}



void range_identifier::may_merge_internal_ranges_in_total_range(){
    if (all_hot_ranges.empty()) return;
    /**
    * merging current total hot ranges
    **/
    for (auto& pair : all_hot_ranges) {
        std::set<hotdb_range*, SortByStartString>& this_range_set = pair.second;
        auto it = this_range_set.begin();
        if(this_range_set.size() == 1){
            continue;
        }
        // fprintf(stdout, "======== Before Merge ========\n");
        // print_range_set(this_range_set);
        while (it != this_range_set.end()) {
            auto start_it = it;
            auto end_it = std::next(it);

            // 找到可以合并的范围
            while (end_it != this_range_set.end() && (*std::prev(end_it))->end_int + 1 == (*end_it)->start_int) {
                (*start_it)->kv_number += (*end_it)->kv_number;
                end_it = std::next(end_it);
            }

            if (std::next(it) != end_it) {
                // 更新第一个范围的 end_str 和 end_int
                auto last_mergeable = std::prev(end_it);
                (*start_it)->end_str = (*last_mergeable)->end_str;
                (*start_it)->end_int = (*last_mergeable)->end_int;
                // 删除中间范围
                auto temp_it = std::next(it);
                while (temp_it != end_it) {
                    auto to_delete = temp_it;
                    temp_it = std::next(temp_it);
                    delete *to_delete;
                    this_range_set.erase(to_delete);
                }
            }

            it = end_it;
        }
        
        // print_range_set(this_range_set);
        // fprintf(stdout, "======== After Merge ========\n\n\n");

    }
}


void range_identifier::may_merge_split_range_to_total_range(){

    /**
    * put current_ranges(current hot ranges) into total_hot_ranges and then check if need to merge
    **/
    auto insert_it = current_ranges.begin(); // 用于遍历current_ranges
    auto current_it = hot_ranges.begin(); // 指向hot_ranges的当前位置
    auto next_it = std::next(current_it); // 指向hot_ranges的下一个位置

    // print_currenthot_ranges();

    while (insert_it != current_ranges.end()) {
        // 找到适当的插入位置
        int64_t set_number = std::stoll((*insert_it)->start_str);
        std::set<hotdb_range*, SortByStartString>& this_range_set = all_hot_ranges[set_number/1000];
        
        // 插入元素
        this_range_set.insert(*insert_it);

        // fprintf(stdout, "Inserted range from current_ranges into hot_ranges - Start: %s, End: %s, KV Num: %lu\n",
        //     (*insert_it)->start_str.c_str(), (*insert_it)->end_str.c_str(), (*insert_it)->kv_number);

        // 移向current_ranges的下一个元素
        ++insert_it;
    }

    // print_hot_ranges();

    may_merge_internal_ranges_in_total_range();

    // print_hot_ranges();
}

void range_identifier::merge_ranges_in_container() {


    for (auto it = temp_container.begin(); it != temp_container.end();) {
        std::string start = *it;
        std::string end = start;
        uint64_t kv_num = keys.at(start);
        long long curr_value = std::stoull(start);

        auto next_it = std::next(it);
        while (next_it != temp_container.end()) {
            long long next_value = std::stoull(*next_it);
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

        // fprintf(stdout, "Created hot range in current_ranges - Start: %s, End: %s, KV Num: %lu range length:%lld\n",
        //         start.c_str(), end.c_str(), kv_num, std::stoll(end) - std::stoll(start) + 1);
        hotdb_range* new_created_range = new hotdb_range(start, end, kv_num);
        current_ranges.push_back(new_created_range);

        // 确保迭代器前进到下一个待处理元素
        it = std::next(it);
    }
}



void range_identifier::create_new_ranges_and_insert() {

    for (auto it = temp_container.begin(); it != temp_container.end();) {
        std::string start = *it;
        std::string end = start;
        uint64_t kv_num = keys.at(start);

        // fprintf(stderr, "Invalid argument: '%s' cannot be converted to unsigned long long.\n", start.c_str());
        unsigned long long curr_value = std::stoull(start);
        

        auto next_it = std::next(it);
        while (next_it != temp_container.end()) {
            long long next_value = std::stoll(*next_it);
            if (next_value == curr_value + 1) {
                end = *next_it;  
                kv_num += keys.at(end);  
                curr_value = next_value;  
                it = next_it;  
                next_it = std::next(it); 
            } else {
                break;  
            }
        }
        

        // fprintf(stdout, "Created hot range ==> Start: %s, End: %s, KV Num: %lu range length:%lld\n",
        //     start.c_str(), end.c_str(), kv_num, std::stoll(end) - std::stoll(start) + 1);
        hotdb_range* created_range = new hotdb_range(start, end, kv_num);
        all_hot_ranges[(created_range->end_int)/1000].insert(created_range);

        it = std::next(it);
    }
}

void range_identifier::check_may_merge_split_range(){
    assert(temp_container.size() != 0);

    if(all_hot_ranges.size()==0){
        create_new_ranges_and_insert();
    }else{
        // print_temp_container();
        for (auto it = temp_container.begin(); it != temp_container.end(); ) {
            bool found = false;
            int64_t set_number = std::stoll(*it);
            std::set<hotdb_range*, SortByStartString>& this_range_set = all_hot_ranges[set_number/1000];
            std::string test_start = *it;
            hotdb_range* new_find_range = new hotdb_range(test_start, test_start, 1);
            // 打印当前要查找的范围
            // fprintf(stdout, "Looking for position to insert: %s\n", test_start.c_str());
            auto find_range = this_range_set.upper_bound(new_find_range);

            if (find_range != this_range_set.begin()) {
                --find_range;
                if ((*find_range)->contains(*it)) {
                    // fprintf(stdout, "Range contains %s: start=%s, end=%s\n",
                    //         test_start.c_str(), (*find_range)->start_str.c_str(), (*find_range)->end_str.c_str());
                    (*find_range)->kv_number += keys[*it];
                    it = temp_container.erase(it); // 移除当前元素，迭代器自动更新到下一个元素
                    delete new_find_range;
                    continue;
                }
            }
            
            it = std::next(it); 
            delete new_find_range;
        }

        merge_ranges_in_container();

        // print_currenthot_ranges();
        if(current_ranges.size() != 0){
            may_merge_split_range_to_total_range();
        }
    }

    // print_hot_ranges();
    
}

void range_identifier::check_and_statistic_ranges(){

    int hot_frequency_per_key = bathc_hot_definition;

    for (int i = 0; i <sorted_keys.size(); ++i) {
        const auto& pair = sorted_keys[i];
        if (pair.second >= hot_frequency_per_key) {
            temp_container.emplace(pair.first);
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

void range_identifier::add_data(const Slice& data){

    std::string key = data.ToString();
    // fprintf(stderr, "Invalid argument: '%s' cannot be converted to unsigned long long.\n", key.c_str());
    unordered_map_increment(key);
    total_number++;


    if(total_number!=0 && total_number%batch_size == 0){
        unordered_map_sort_merge_data(); 
        check_and_statistic_ranges(); 
        keys.clear();
        sorted_keys.clear();
        sorted_by_keys.clear();
        temp_container.clear();
        current_ranges.clear();
        // fprintf(stderr,"total_number:%ld\n",total_number);
        // print_total_ranges();
    }
}


bool range_identifier::is_hot(const Slice& key) {
    int64_t key_int = std::stoll(key.ToString());
    std::set<hotdb_range*, SortByStartString>& this_range_set = all_hot_ranges[key_int/1000];
    std::string test_start = key.ToString();
    hotdb_range temp_find_range(test_start, test_start, 1);
    auto find_range = this_range_set.upper_bound(&temp_find_range);

    if (find_range != this_range_set.begin()) {
        --find_range;
        if ((*find_range)->contains(test_start)) {
            return true;
        }
    }
    return false; 
}

bool range_identifier::is_hot2(const Slice& key) {
    if(std::stoll(key.ToString()) < 10000){
        return true;
    }
    return false; 
}

int range_identifier::get_current_num_kv() const{
    return keys.size();
}

}  // namespace leveldb
