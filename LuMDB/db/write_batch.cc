// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// WriteBatch::rep_ :=
//    sequence: fixed64
//    count: fixed32
//    data: record[count]
// record :=
//    kTypeValue varstring varstring         |
//    kTypeDeletion varstring
// varstring :=
//    len: varint32
//    data: uint8[len]

#include "leveldb/write_batch.h"

#include "db/dbformat.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "util/coding.h"
#include "db/range_merge_split.h"
#include "util/logging.h"
#include "leveldb/env.h"

namespace leveldb {

// WriteBatch header has an 8-byte sequence number followed by a 4-byte count.
static const size_t kHeader = 12;

WriteBatch::WriteBatch() { Clear(); }

WriteBatch::~WriteBatch() = default;

WriteBatch::Handler::~Handler() = default;

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(kHeader);
}

size_t WriteBatch::ApproximateSize() const { return rep_.size(); }

Status WriteBatch::Iterate(Handler* handler) const {
  Slice input(rep_);
  if (input.size() < kHeader) {
    return Status::Corruption("malformed WriteBatch (too small)");
  }

  input.remove_prefix(kHeader);
  Slice key, value;
  int found = 0;
  while (!input.empty()) {
    found++;
    char tag = input[0];
    input.remove_prefix(1);
    switch (tag) {
      case kTypeValue:
        if (GetLengthPrefixedSlice(&input, &key) &&
            GetLengthPrefixedSlice(&input, &value)) {
          handler->Put(key, value);
        } else {
          return Status::Corruption("bad WriteBatch Put");
        }
        break;
      case kTypeDeletion:
        if (GetLengthPrefixedSlice(&input, &key)) {
          handler->Delete(key);
        } else {
          return Status::Corruption("bad WriteBatch Delete");
        }
        break;
      default:
        return Status::Corruption("unknown WriteBatch tag");
    }
  }
  if (found != WriteBatchInternal::Count(this)) {
    return Status::Corruption("WriteBatch has wrong count");
  } else {
    // fprintf(stderr, "%d entries founded in this batch!\n", found);
    return Status::OK();
  }
  
}

int WriteBatchInternal::Count(const WriteBatch* b) {
  return DecodeFixed32(b->rep_.data() + 8);
}

void WriteBatchInternal::SetCount(WriteBatch* b, int n) {
  EncodeFixed32(&b->rep_[8], n);
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* b) {
  return SequenceNumber(DecodeFixed64(b->rep_.data()));
}

void WriteBatchInternal::SetSequence(WriteBatch* b, SequenceNumber seq) {
  EncodeFixed64(&b->rep_[0], seq);
}

void WriteBatch::Put(const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedSlice(&rep_, key);
  PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatch::Delete(const Slice& key) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeDeletion));
  PutLengthPrefixedSlice(&rep_, key);
}

void WriteBatch::Append(const WriteBatch& source) {
  WriteBatchInternal::Append(this, &source);
}

namespace {
class MemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  MemTable* mem_;
  MemTable* hot_mem_;
  HotRangesContext* hot_identifier;

  int mem_count;          // Counter for mem_
  int hot_mem_count;      // Counter for hot_mem_
  int hot_intensity_count; // Counter for hot intensity data
  int hot_range_count;    // Counter for hot range data
  int cold_data_count;    // Counter for cold data

  MemTableInserter()
    : sequence_(0), mem_(nullptr), hot_mem_(nullptr), hot_identifier(nullptr),
    mem_count(0), hot_mem_count(0), hot_intensity_count(0), hot_range_count(0), cold_data_count(0) {}

  void Put(const Slice& key, const Slice& value) override {

    if(!hot_identifier->hot_ranges_.empty()){
      hot_range new_hot_range(key.data(), key.size(), key.data(), key.size());
      // fprintf(stderr, "min_max_range start: %.*s, end: %.*s\n",
      //   static_cast<int>(hot_identifier->min_max_range->start_size),
      //   hot_identifier->min_max_range->start_ptr,
      //   static_cast<int>(hot_identifier->min_max_range->end_size),
      //   hot_identifier->min_max_range->end_ptr);

      if(hot_identifier->min_max_range->CompareWithEnd(key.data(), key.size()) < 0 ||
        hot_identifier->min_max_range->CompareWithBegin(key.data(), key.size()) > 0){
        // fprintf(stderr, "Key: %.*s, Result: Outside min_max_range\n\n", static_cast<int>(key.size()), key.data());
        mem_->Add(sequence_, kTypeValue, key, value);
        mem_count++;
        sequence_++;
        return;
      }

      if(hot_identifier->largest_intensity_range->is_key_contains(key.data(), key.size())){
        hot_mem_->Add(sequence_, kTypeValue, key, value);
        hot_intensity_count++;  // Increment hot intensity data counter
        sequence_++;
        return;
      }

      auto it = hot_identifier->hot_ranges_.upper_bound(new_hot_range);
      if(it!= hot_identifier->hot_ranges_.begin()){
        --it;
        if(it->is_key_contains(key.data(), key.size())){
          hot_mem_->Add(sequence_, kTypeValue, key, value);
          hot_range_count++;  // Increment hot range data counters
        }else{
          mem_->Add(sequence_, kTypeValue, key, value);
          cold_data_count++;  // Increment cold data counter
        }
      }
      sequence_++;
      return;

    }else{
      mem_->Add(sequence_, kTypeValue, key, value);
      cold_data_count++;  // Increment cold data counter
    }
    // fprintf(stderr, "key:%s,  unordered_map:%d, hot key count: %d cold key count: %d \n", key.ToString().c_str(), (*batch_data_map)[key.ToString()],hot_mem_count, mem_count);
    sequence_++;
  }

  void Delete(const Slice& key) override {
    if(hot_identifier == nullptr || hot_mem_ == nullptr ){
      mem_->Add(sequence_, kTypeDeletion, key, Slice());
    }else{
      mem_->Add(sequence_, kTypeDeletion, key, Slice());
    }
    sequence_++;
  }
};

class MultiMemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  MemTable* mem_;
  MemTable* hot_mem_;
  range_identifier* hot_identifier;
  std::set<mem_partition_guard*, PartitionGuardComparator>* mem_partitions_set;
  std::unordered_map<std::string, int>* batch_data_map;
  Comparator* user_comparator;

  int mem_count;  // Counter for mem_
  int hot_mem_count;  // Counter for hot_mem_

  MultiMemTableInserter()
      : sequence_(0), mem_(nullptr), hot_mem_(nullptr), hot_identifier(nullptr), mem_count(0), hot_mem_count(0) {}

  void Put(const Slice& key, const Slice& value) override {
    assert(hot_identifier != nullptr);
    assert(hot_mem_ != nullptr);

    if(hot_identifier->is_hot(key) == true){
      hot_mem_->Add(sequence_, kTypeValue, key, value);
      hot_mem_count++;
    }else{

      mem_partition_guard* target_partition = nullptr;
      std::string test_start = key.ToString();
      auto it = mem_partitions_set->upper_bound(new mem_partition_guard(test_start, test_start));
      if (it != mem_partitions_set->begin()) {
        --it;
        if ( (*it)->contains(test_start)) {
          target_partition = *it;
        }
      }

      if (target_partition == nullptr) {
        mem_->Add(sequence_, kTypeValue, key, value);
        return;
      }else{
        // target_partition->partition_mem->Add(sequence_, kTypeValue, key, value);
      }
      mem_count++;
    }
    sequence_++;  
    // fprintf(stderr, "key:%s,  unordered_map:%d, hot key count: %d cold key count: %d \n", 
        // key.ToString().c_str(), (*batch_data_map)[key.ToString()],hot_mem_count, mem_count);
  }
    
  void Delete(const Slice& key) override {
    if(hot_identifier == nullptr || hot_mem_ == nullptr ){
      mem_->Add(sequence_, kTypeDeletion, key, Slice());
    }else{
      if(hot_identifier->is_hot(key) == true){
        hot_mem_->Add(sequence_, kTypeDeletion, key, Slice());
      }else{
        mem_->Add(sequence_, kTypeDeletion, key, Slice());
      }
    }
    sequence_++;
  }
};

}  // namespace

Status WriteBatchInternal::InsertInto(const WriteBatch* b, MemTable* memtable, MemTable* hot_memtable, Logger* info_loger, HotRangesContext* hot_indentifier) {
  MemTableInserter inserter;
  inserter.sequence_ = WriteBatchInternal::Sequence(b);
  inserter.mem_ = memtable;
  inserter.hot_mem_ = hot_memtable;
  inserter.hot_identifier = hot_indentifier;
  Status s = b->Iterate(&inserter);

  if (info_loger != nullptr) {
  Log(info_loger, "InsertInto: hot_mem_ added %d entries, mem_ added %d entries, Hot Intensity Data Count: %d, Hot Range Data Count: %d, Cold Data Count: %d",
    inserter.hot_mem_count, inserter.mem_count, inserter.hot_intensity_count, inserter.hot_range_count, inserter.cold_data_count);
  }
  return s;
}


Status WriteBatchInternal::InsertInto(const WriteBatch* b, std::set<mem_partition_guard*, PartitionGuardComparator>* memtables, MemTable* hot_memtable, range_identifier* hot_key_idetiifer, Logger* info_loger){
  MultiMemTableInserter inserter;
  inserter.sequence_ = WriteBatchInternal::Sequence(b);
  inserter.mem_partitions_set = memtables;
  inserter.hot_mem_ = hot_memtable;
  inserter.hot_identifier = hot_key_idetiifer;
  Status s = b->Iterate(&inserter);

  if(info_loger!= nullptr)
    Log(info_loger, "InsertInto: hot_mem_ added %d entries, mem_ added %d entries", inserter.hot_mem_count, inserter.mem_count);

  return s;
}

void WriteBatchInternal::SetContents(WriteBatch* b, const Slice& contents) {
  assert(contents.size() >= kHeader);
  b->rep_.assign(contents.data(), contents.size());
}

void WriteBatchInternal::Append(WriteBatch* dst, const WriteBatch* src) {
  SetCount(dst, Count(dst) + Count(src));
  assert(src->rep_.size() >= kHeader);
  dst->rep_.append(src->rep_.data() + kHeader, src->rep_.size() - kHeader);
}

}  // namespace leveldb
