// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_set.h"

#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "db/table_cache.h"
#include <algorithm>
#include <cstdio>

#include "leveldb/env.h"
#include "leveldb/table_builder.h"

#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/logging.h"

namespace leveldb {

namespace TieringSpaceConfig{

  const double kTieringLevelSizes[] = {
    10. * 1048576.0, // Level 0
    128 * 1048576.0, // Level 1
    512 * 1048576.0, // Level 2
    4096 * 1048576.0, // Level 3
    32768 * 1048576.0, // Level 4
    262144 * 1048576.0, // Level 5
    2097152 * 1048576.0 // Level 6
    // Add more levels if needed
  };

}


static size_t TargetFileSize(const Options* options) {
  return options->max_file_size;
}

static size_t TargetMinFileSize(const Options* options) {
  return options->min_file_size;
}

static size_t TargetTieringFileSize(const Options* options) {
  return options->max_tiering_file_size;
}

// Maximum bytes of overlaps in grandparent (i.e., level+2) before we
// stop building a single file in a level->level+1 compaction.
static int64_t MaxGrandParentOverlapBytes(const Options* options) {
  return 10 * TargetFileSize(options);
}

// Maximum number of bytes in all compacted files.  We avoid expanding
// the lower level file set of a compaction if it would make the
// total compaction cover more than this many bytes.
static int64_t ExpandedCompactionByteSizeLimit(const Options* options) {
  return 25 * TargetFileSize(options);
}

static double MaxBytesForLevel(const Options* options, int level) {
  // Note: the result for level zero is not really used since we set
  // the level-0 compaction threshold based on number of files.

  // Result for both level-0 and level-1
  double result = 10. * 1048576.0;
  while (level > 1) {
    result *= 10;
    level--;
  }
  return result;
}



static uint64_t MaxFileSizeForLevel(const Options* options, int level) {
  // We could vary per level to reduce number of files?
  return TargetFileSize(options);
}

static uint64_t MinFileSizeForLevel(const Options* options, int level) {
  // We could vary per level to reduce number of files?
  return TargetMinFileSize(options);
}

static uint64_t MaxTieringFileSizeForLevel(const Options* options, int level) {
  // We could vary per level to reduce number of files?
  return TargetTieringFileSize(options);
}


/**
 * @brief Calculate the total file size for leveling files.
 *
 * This function sums up the sizes of all files in the provided vector of FileMetaData pointers.
 *
 * @param files The vector of FileMetaData pointers.
 * @return The total size of the files.
 */
static int64_t TotalFileSize(const std::vector<FileMetaData*>& files) {
  int64_t sum = 0;
  for (size_t i = 0; i < files.size(); i++) {
    sum += files[i]->file_size;
  }
  return sum;
}


static int64_t TotalFileSize(const std::map<int, std::vector<FileMetaData*>>& files) {
  int64_t sum = 0;
  for (const auto& run : files) {
    for (size_t i = 0; i < run.second.size(); i++) {
      sum += run.second[i]->file_size;
    }
  }
  return sum;
}

static int64_t TotalFileSize(const std::map<uint64_t, std::vector<FileMetaData*>>& files) {
  int64_t sum = 0;
  for (const auto& run : files) {
    for (size_t i = 0; i < run.second.size(); i++) {
      sum += run.second[i]->file_size;
    }
  }
  return sum;
}

/**
 * @brief Calculate the total file size for tiering files.
 *
 * This function sums up the sizes of all actual files in the provided vector of Logica_File_MetaData pointers.
 *
 * @param logical_files The vector of Logica_File_MetaData pointers.
 * @return The total size of the actual files.
 */
static int64_t TotalFileSize(const std::vector<Logica_File_MetaData*>& logical_files) {
  int64_t sum = 0;
  for (size_t i = 0; i < logical_files.size(); i++) {
    sum += logical_files[i]->file_size;
  }
  return sum;
}

Version::~Version() {
  assert(refs_ == 0);

  // Remove from linked list
  prev_->next_ = next_;
  next_->prev_ = prev_;

  // Drop references to files
  for (int level = 0; level < config::kNumLevels; level++) {
    for (size_t i = 0; i < leveling_files_[level].size(); i++) {
      FileMetaData* f = leveling_files_[level][i];
      assert(f->refs > 0);
      f->refs--;
      if (f->refs <= 0) {
        delete f;
      }
    }

    for(auto every_single_run: tiering_runs_[level]){
      for (size_t i = 0; i < every_single_run.second.size(); i++) {
        FileMetaData* f = every_single_run.second[i];
        assert(f->refs > 0);
        f->refs--;
        if (f->refs <= 0) {
          delete f;
        }
      }
    }
  }
}

int FindFile(const InternalKeyComparator& icmp,
             const std::vector<FileMetaData*>& files, const Slice& key) {
  uint32_t left = 0;
  uint32_t right = files.size();
  while (left < right) {
    uint32_t mid = (left + right) / 2;
    const FileMetaData* f = files[mid];
    if (icmp.InternalKeyComparator::Compare(f->largest.Encode(), key) < 0) {
      // Key at "mid.largest" is < "target".  Therefore all
      // files at or before "mid" are uninteresting.
      left = mid + 1;
    } else {
      // Key at "mid.largest" is >= "target".  Therefore all files
      // after "mid" are uninteresting.
      right = mid;
    }
  }
  return right;
}

uint32_t FindLogicalFile(const InternalKeyComparator& icmp,
                         const std::vector<Logica_File_MetaData*>& logical_files,
                         const Slice& key) {
  uint32_t left = 0;
  uint32_t right = logical_files.size();
  while (left < right) {
    uint32_t mid = (left + right) / 2;
    if (icmp.Compare(logical_files[mid]->smallest.Encode(), key) <= 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}


static bool AfterFile(const Comparator* ucmp, const Slice* user_key, const FileMetaData* f) {

  // null user_key occurs before all keys and is therefore never after *f
  return (user_key != nullptr && ucmp->Compare(*user_key, f->largest.user_key()) > 0);

}

bool AfterFile(const Comparator* ucmp, const Slice* user_key, const Logica_File_MetaData* f) {
  // Returns true if user_key > f's largest user key.
  return (user_key != nullptr && ucmp->Compare(*user_key, f->largest.user_key()) > 0);
}


static bool BeforeFile(const Comparator* ucmp, const Slice* user_key, const FileMetaData* f) {

  // null user_key occurs after all keys and is therefore never before *f
  return (user_key != nullptr && ucmp->Compare(*user_key, f->smallest.user_key()) < 0);
}


static bool BeforeFile(const Comparator* ucmp, const Slice* user_key, const Logica_File_MetaData* f) {

  // null user_key occurs after all keys and is therefore never before *f
  return (user_key != nullptr && ucmp->Compare(*user_key, f->smallest.user_key()) < 0);
}

bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<FileMetaData*>& files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key) {

  const Comparator* ucmp = icmp.user_comparator();

  if (!disjoint_sorted_files) {
    // Need to check against all files

    for (size_t i = 0; i < files.size(); i++) {
      const FileMetaData* f = files[i];
      if (AfterFile(ucmp, smallest_user_key, f) ||
          BeforeFile(ucmp, largest_user_key, f)) {
        // No overlap
      } else {
        return true;  // Overlap
      }
    }

    return false;
  }

  // Binary search over file list
  uint32_t index = 0;
  if (smallest_user_key != nullptr) {
    // Find the earliest possible internal key for smallest_user_key
    InternalKey small_key(*smallest_user_key, kMaxSequenceNumber,
                          kValueTypeForSeek);
    index = FindFile(icmp, files, small_key.Encode());
  }

  if (index >= files.size()) {
    // beginning of range is after all files, so no overlap.
    return false;
  }

  return !BeforeFile(ucmp, largest_user_key, files[index]);
}


bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<Logica_File_MetaData*>& logical_files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key) {

  const Comparator* ucmp = icmp.user_comparator();

  if (!disjoint_sorted_files) {
    // Need to check against all logical files
    for (size_t i = 0; i < logical_files.size(); i++) {
      const Logica_File_MetaData* logical_file = logical_files[i];
        if (AfterFile(ucmp, smallest_user_key, logical_file) ||
            BeforeFile(ucmp, largest_user_key, logical_file)) {
          // No overlap
        } else {
          return true;  // Overlap
        }
    }
    return false;
  }

  // Binary search over logical file list
  uint32_t index = 0;
  if (smallest_user_key != nullptr) {
    // Find the earliest possible internal key for smallest_user_key
    InternalKey small_key(*smallest_user_key, kMaxSequenceNumber,  kValueTypeForSeek);
    index = FindLogicalFile(icmp, logical_files, small_key.Encode());
  }

  if (index >= logical_files.size()) {
    // beginning of range is after all logical files, so no overlap.
    return false;
  }

  const Logica_File_MetaData* logical_file = logical_files[index];
  for (const auto& file : logical_file->actual_files) {
    if (!BeforeFile(ucmp, largest_user_key, &file)) {
      return true;  // Overlap
    }
  }

  return false;
}





// An internal iterator.  For a given version/level pair, yields
// information about the files in the level.  For a given entry, key()
// is the largest key that occurs in the file, and value() is an
// 16-byte value containing the file number and file size, both
// encoded using EncodeFixed64.
class Version::LevelFileNumIterator : public Iterator {
 public:
  LevelFileNumIterator(const InternalKeyComparator& icmp,
                       const std::vector<FileMetaData*>* flist)
      : icmp_(icmp), flist_(flist), index_(flist->size()) {  // Marks as invalid
  }
  bool Valid() const override { return index_ < flist_->size(); }
  void Seek(const Slice& target) override {
    index_ = FindFile(icmp_, *flist_, target);
  }
  void SeekToFirst() override { index_ = 0; }
  void SeekToLast() override {
    index_ = flist_->empty() ? 0 : flist_->size() - 1;
  }
  void Next() override {
    assert(Valid());
    index_++;
  }
  void Prev() override {
    assert(Valid());
    if (index_ == 0) {
      index_ = flist_->size();  // Marks as invalid
    } else {
      index_--;
    }
  }
  Slice key() const override {
    assert(Valid());
    return (*flist_)[index_]->largest.Encode();
  }
  Slice value() const override {
    assert(Valid());
    EncodeFixed64(value_buf_, (*flist_)[index_]->number);
    EncodeFixed64(value_buf_ + 8, (*flist_)[index_]->file_size);
    return Slice(value_buf_, sizeof(value_buf_));
  }
  Status status() const override { return Status::OK(); }

 private:
  const InternalKeyComparator icmp_;
  const std::vector<FileMetaData*>* const flist_;
  uint32_t index_;

  // Backing store for value().  Holds the file number and size.
  mutable char value_buf_[16];
};

static Iterator* GetFileIterator(void* arg, const ReadOptions& options,
                                 const Slice& file_value) {
  TableCache* cache = reinterpret_cast<TableCache*>(arg);
  if (file_value.size() != 16) {
    return NewErrorIterator(
        Status::Corruption("FileReader invoked with unexpected value"));
  } else {
    return cache->NewIterator(options, DecodeFixed64(file_value.data()),
                              DecodeFixed64(file_value.data() + 8));
  }
}

Iterator* Version::NewConcatenatingIterator(const ReadOptions& options,
                                            int level) const {
  return NewTwoLevelIterator(
      new LevelFileNumIterator(vset_->icmp_, &leveling_files_[level]), &GetFileIterator,
      vset_->table_cache_, options);
}


Iterator* Version::NewPartitionConcatenatingIterator(uint64_t partition_number, const ReadOptions& options,
                                            int level) const {
  return NewTwoLevelIterator(
      new LevelFileNumIterator(vset_->icmp_, &partitioning_leveling_files_[level].at(partition_number)), &GetFileIterator,
      vset_->table_cache_, options);
}

void Version::AddIterators(const ReadOptions& options,
                           std::vector<Iterator*>* iters) {
  // Merge all level zero files together since they may overlap
  for (size_t i = 0; i < leveling_files_[0].size(); i++) {
    iters->push_back(vset_->table_cache_->NewIterator(
        options, leveling_files_[0][i]->number, leveling_files_[0][i]->file_size));
  }

  // For levels > 0, we can use a concatenating iterator that sequentially
  // walks through the non-overlapping files in the level, opening them
  // lazily.
  for (int level = 1; level < config::kNumLevels; level++) {
    if (!leveling_files_[level].empty()) {
      iters->push_back(NewConcatenatingIterator(options, level));
    }
  }
}

void Version::AddIterators(uint64_t parent_partition, uint64_t sub_partition, const ReadOptions& options,
                           std::vector<Iterator*>* iters) {

  // Merge all level zero files together since they may overlap
  uint64_t partition_number;
  for(int level=0; level <2; level++){
    partition_number = (level==0?parent_partition:sub_partition);
    for (size_t i = 0; i < partitioning_leveling_files_[level][partition_number].size(); i++) {
      iters->push_back(vset_->table_cache_->NewIterator(
          options, partitioning_leveling_files_[level][partition_number][i]->number, partitioning_leveling_files_[level][partition_number][i]->file_size));
    }
  }
  

  // For levels > 0, we can use a concatenating iterator that sequentially
  // walks through the non-overlapping files in the level, opening them
  // lazily.
  for (int level = 2; level < config::kNumLevels; level++) {
    if (!partitioning_leveling_files_[level][partition_number].empty()) {
      iters->push_back(NewPartitionConcatenatingIterator(partition_number, options, level));
    }
  }
}

// Callback from TableCache::Get()
namespace {
enum SaverState {
  kNotFound,
  kFound,
  kDeleted,
  kCorrupt,
};
struct Saver {
  SaverState state;
  const Comparator* ucmp;
  Slice user_key;
  std::string* value;
};
}  // namespace
static void SaveValue(void* arg, const Slice& ikey, const Slice& v) {
  Saver* s = reinterpret_cast<Saver*>(arg);
  ParsedInternalKey parsed_key;
  if (!ParseInternalKey(ikey, &parsed_key)) {
    s->state = kCorrupt;
  } else {
    if (s->ucmp->Compare(parsed_key.user_key, s->user_key) == 0) {
      s->state = (parsed_key.type == kTypeValue) ? kFound : kDeleted;
      if (s->state == kFound) {
        s->value->assign(v.data(), v.size());
      }
    }
  }
}

static bool NewestFirst(FileMetaData* a, FileMetaData* b) {
  return a->number > b->number;
}


void Version::ForEachOverlappingTiering(Slice user_key, Slice internal_key, void* arg,
                                 bool (*func)(void*, int, FileMetaData*)) {

  int64_t start_time = vset_->env_->NowMicros(); // 开始查找metadata的时间                                  
  const Comparator* ucmp = vset_->icmp_.user_comparator();

  // Search level-0 in order from newest to oldest.
  std::vector<FileMetaData*> tmp;
  tmp.reserve(tiering_runs_[0][0].size());
  for (uint32_t i = 0; i < tiering_runs_[0][0].size(); i++) {
    FileMetaData* f = tiering_runs_[0][0][i];
    if (ucmp->Compare(user_key, f->smallest.user_key()) >= 0 &&
        ucmp->Compare(user_key, f->largest.user_key()) <= 0) {
      tmp.push_back(f);
    }
  }
  
  int64_t end_time ; 

  if (!tmp.empty()) {
    std::sort(tmp.begin(), tmp.end(), NewestFirst);
    end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
    vset_->search_stats.level0_search_time += (end_time - start_time);  // 记录时间
    for (uint32_t i = 0; i < tmp.size(); i++) {
      if (!(*func)(arg, 0, tmp[i])) {
        end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
        vset_->search_stats.total_time += (end_time - start_time);
        return;
      }
    }
  }else{
    end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
    vset_->search_stats.level0_search_time += (end_time - start_time);  // 记录时间
  }

  // check the level_0 files in the first runs, because there is only one run in tiering



  // Search other levels.
  for (int level = 1; level < config::kNumLevels; level++) {
    size_t num_files = leveling_files_[level].size();
    if (num_files == 0) continue;

    start_time = vset_->env_->NowMicros(); // 开始查找metadata的时间
    // Binary search to find earliest index whose largest key >= internal_key.
    uint32_t index = FindFile(vset_->icmp_, leveling_files_[level], internal_key);
    end_time = vset_->env_->NowMicros(); // 结束查找metadata的时间
    vset_->search_stats.other_levels_search_time += (end_time - start_time);
    if (index < num_files) {
      FileMetaData* f = leveling_files_[level][index];
      if (ucmp->Compare(user_key, f->smallest.user_key()) < 0) {
        // All of "f" is past any data for user_key
      } else {
        if (!(*func)(arg, level, f)) {
          end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
          vset_->search_stats.total_time += (end_time - start_time);
          return;
        }
      }
    }
  }
}

void Version::ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                                 bool (*func)(void*, int, FileMetaData*), uint64_t partition_num, uint64_t sub_partition_num ) {

  int64_t start_time = vset_->env_->NowMicros(); // 开始查找metadata的时间                                  
  const Comparator* ucmp = vset_->icmp_.user_comparator();

  // Search level-0 in order from newest to oldest.
  std::vector<FileMetaData*> tmp;
  tmp.reserve(partitioning_leveling_files_[partition_num][0].size());
  for(int l = 0; l<2; l++){
    uint64_t check_partition_num = l==0?partition_num:sub_partition_num;
    for (uint32_t i = 0; i < partitioning_leveling_files_[l][check_partition_num].size(); i++) {
      FileMetaData* f = partitioning_leveling_files_[l][check_partition_num][i];
          // fprintf(stdout, "Checking file %u: smallest = %s, largest = %s\n",
          //   i,f->smallest.user_key().ToString().c_str(),f->largest.user_key().ToString().c_str());
      if (ucmp->Compare(user_key, f->smallest.user_key()) >= 0 &&
          ucmp->Compare(user_key, f->largest.user_key()) <= 0) {
        // fprintf(stdout, "File %u matches with user_key: %s\n Checking file %lu: smallest = %s, largest = %s\n",
        //       i, user_key.ToString().c_str(),f->number,f->smallest.user_key().ToString().c_str(), f->largest.user_key().ToString().c_str() );
        tmp.push_back(f);
      }
    }
  }

  // fprintf(stdout, "There is %lu files need to be checked!\n", tmp.size());
  
  int64_t end_time ; 
  if (!tmp.empty()) {
    std::sort(tmp.begin(), tmp.end(), NewestFirst);
    end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
    vset_->search_stats.level0_search_time += (end_time - start_time);  // 记录时间
    for (uint32_t i = 0; i < tmp.size(); i++) {
      if (!(*func)(arg, 0, tmp[i])) {
        end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
        vset_->search_stats.total_time += (end_time - start_time);
        return;
      }
    }
  }else{
    end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
    vset_->search_stats.level0_search_time += (end_time - start_time);  // 记录时间
  }

  // Search other levels.
  for (int level = 2; level < config::kNumLevels; level++) {
    size_t num_files = partitioning_leveling_files_[level][sub_partition_num].size();
    if (num_files == 0) continue;

    start_time = vset_->env_->NowMicros(); // 开始查找metadata的时间

    // Binary search to find earliest index whose largest key >= internal_key.
    uint32_t index = FindFile(vset_->icmp_, partitioning_leveling_files_[level][sub_partition_num], internal_key);

    end_time = vset_->env_->NowMicros(); // 结束查找metadata的时间
    vset_->search_stats.other_levels_search_time += (end_time - start_time);

    if (index < num_files) {
      FileMetaData* f = partitioning_leveling_files_[level][sub_partition_num][index];
      if (ucmp->Compare(user_key, f->smallest.user_key()) < 0) {
        // All of "f" is past any data for user_key
      } else {
        if (!(*func)(arg, level, f)) {
          end_time = vset_->env_->NowMicros();  // 结束查找Level 0 metadata的时间
          vset_->search_stats.total_time += (end_time - start_time);
          return;
        }
      }
    }
  }
}

Status Version::Get(const ReadOptions& options, const LookupKey& k,
                    std::string* value, GetStats* stats, uint64_t partition_num, uint64_t sub_partition_num) {

  stats->seek_file = nullptr;
  stats->seek_file_level = -1;

  struct State {
    Saver saver;
    GetStats* stats;
    const ReadOptions* options;
    Slice ikey;
    FileMetaData* last_file_read;
    int last_file_read_level;

    VersionSet* vset;
    Status s;
    bool found;

    static bool Match(void* arg, int level, FileMetaData* f) {
      State* state = reinterpret_cast<State*>(arg);

      if (state->stats->seek_file == nullptr &&
          state->last_file_read != nullptr) {
        // We have had more than one seek for this read.  Charge the 1st file.
        state->stats->seek_file = state->last_file_read;
        state->stats->seek_file_level = state->last_file_read_level;
      }

      state->last_file_read = f;
      state->last_file_read_level = level;

      state->s = state->vset->table_cache_->Get(*state->options, f->number,
                                                f->file_size, state->ikey,
                                                &state->saver, SaveValue);
      if (!state->s.ok()) {
        state->found = true;
        return false;
      }
      switch (state->saver.state) {
        case kNotFound:
          return true;  // Keep searching in other files
        case kFound:
          state->found = true;
          return false;
        case kDeleted:
          return false;
        case kCorrupt:
          state->s =
              Status::Corruption("corrupted key for ", state->saver.user_key);
          state->found = true;
          return false;
      }

      // Not reached. Added to avoid false compilation warnings of
      // "control reaches end of non-void function".
      return false;
    }
  };

  State state;
  state.found = false;
  state.stats = stats;
  state.last_file_read = nullptr;
  state.last_file_read_level = -1;

  state.options = &options;
  state.ikey = k.internal_key();
  state.vset = vset_;

  state.saver.state = kNotFound;
  state.saver.ucmp = vset_->icmp_.user_comparator();
  state.saver.user_key = k.user_key();
  state.saver.value = value;

  ForEachOverlapping(state.saver.user_key, state.ikey, &state, &State::Match, partition_num, sub_partition_num);

  return state.found ? state.s : Status::NotFound(Slice());
}



Status Version::Get_Hot(const ReadOptions& options, const LookupKey& k,
                    std::string* value, GetStats* stats) {

  stats->seek_file = nullptr;
  stats->seek_file_level = -1;

  struct State_Hot {
    Saver saver;
    GetStats* stats;
    const ReadOptions* options;
    Slice ikey;
    FileMetaData* last_file_read;
    int last_file_read_level;

    VersionSet* vset;
    Status s;
    bool found;

    static bool Match(void* arg, int level, FileMetaData* f) {
      State_Hot* state = reinterpret_cast<State_Hot*>(arg);

      if (state->stats->seek_file == nullptr &&
          state->last_file_read != nullptr) {
        // We have had more than one seek for this read.  Charge the 1st file.
        state->stats->seek_file = state->last_file_read;
        state->stats->seek_file_level = state->last_file_read_level;
      }

      state->last_file_read = f;
      state->last_file_read_level = level;

      state->s = state->vset->table_cache_->Get(*state->options, f->number,
                                                f->file_size, state->ikey,
                                                &state->saver, SaveValue);
      if (!state->s.ok()) {
        state->found = true;
        return false;
      }
      switch (state->saver.state) {
        case kNotFound:
          return true;  // Keep searching in other files
        case kFound:
          state->found = true;
          return false;
        case kDeleted:
          return false;
        case kCorrupt:
          state->s =
              Status::Corruption("corrupted key for ", state->saver.user_key);
          state->found = true;
          return false;
      }

      // Not reached. Added to avoid false compilation warnings of
      // "control reaches end of non-void function".
      return false;
    }
  };

  State_Hot state;
  state.found = false;
  state.stats = stats;
  state.last_file_read = nullptr;
  state.last_file_read_level = -1;

  state.options = &options;
  state.ikey = k.internal_key();
  state.vset = vset_;

  state.saver.state = kNotFound;
  state.saver.ucmp = vset_->icmp_.user_comparator();
  state.saver.user_key = k.user_key();
  state.saver.value = value;

  ForEachOverlappingTiering(state.saver.user_key, state.ikey, &state, &State_Hot::Match);

  return state.found ? state.s : Status::NotFound(Slice());
}



// ==== Start of modified code ====
int Version::NumFiles(int level) const {
  return leveling_files_[level].size()+NumTieringFilesInLevel(level);
}

int Version::NumLevelingFiles(int level) const {
  return leveling_files_[level].size();
}

int Version::NumPartitioningFiles(int level) const {

  size_t total_files = 0;
  const auto& partition_files = partitioning_leveling_files_[level];
  for (const auto& partition : partition_files) {
    total_files += partition.second.size();
  }
  return total_files;
  
}

int Version::NumTieringFiles(int level) const {
  return NumTieringFilesInLevel(level);
}

int Version::NumTieringFilesInLevel(int level) const {
  int num_files_in_level = 0;
  for (const auto& run : tiering_runs_[level]) {
    num_files_in_level += run.second.size();
  }
  return num_files_in_level;
}



int Version::NumTieringFilesInLevelAndRun(int level, int run) const{
  const auto& runs = tiering_runs_[level];
  auto it = runs.find(run);
  if (it != runs.end()) {
    return it->second.size();
  }
  return -1;
}

void Version::InitializeTieringRuns() {
  for (int level = 0; level < config::kNumLevels; ++level) {
    tiering_runs_[level].clear();
    if(level == 0){
      tiering_runs_[level][0] = std::vector<FileMetaData*>();
      continue;
    }
    for (int run = 0; run < config::kNum_SortedRuns; ++run) {
      tiering_runs_[level][run] = std::vector<FileMetaData*>();
    }
  }
}
// ==== End of modified code ====




bool Version::UpdateStats(const GetStats& stats) {
  FileMetaData* f = stats.seek_file;
  if (f != nullptr) {
    f->allowed_seeks--;
    if (f->allowed_seeks <= 0 && file_to_compact_in_leveling == nullptr) {
      file_to_compact_in_leveling = f;
      file_to_compact_level_in_leveling = stats.seek_file_level;
      return true;
    }
  }
  return false;
}

bool Version::RecordReadSample(Slice internal_key) {
  ParsedInternalKey ikey;
  if (!ParseInternalKey(internal_key, &ikey)) {
    return false;
  }

  struct State {
    GetStats stats;  // Holds first matching file
    int matches;

    static bool Match(void* arg, int level, FileMetaData* f) {
      State* state = reinterpret_cast<State*>(arg);
      state->matches++;
      if (state->matches == 1) {
        // Remember first match.
        state->stats.seek_file = f;
        state->stats.seek_file_level = level;
      }
      // We can stop iterating once we have a second match.
      return state->matches < 2;
    }
  };

  State state;
  state.matches = 0;
  ForEachOverlapping(ikey.user_key, internal_key, &state, &State::Match);

  // Must have at least two matches since we want to merge across
  // files. But what if we have a single file that contains many
  // overwrites and deletions?  Should we have another mechanism for
  // finding such files?
  if (state.matches >= 2) {
    // 1MB cost is about 1 seek (see comment in Builder::Apply).
    return UpdateStats(state.stats);
  }
  return false;
}

void Version::Ref() { ++refs_; }

void Version::Unref() {
  assert(this != &vset_->dummy_versions_);
  assert(refs_ >= 1);
  --refs_;
  if (refs_ == 0) {
    delete this;
  }
}

bool Version::OverlapInLevel(int level, const Slice* smallest_user_key,
                             const Slice* largest_user_key) {
  return SomeFileOverlapsRange(vset_->icmp_, (level > 0), leveling_files_[level],
                               smallest_user_key, largest_user_key);
}

// 找一个合适的level放置新从memtable dump出的sstable
// 注：不一定总是放到level 0，尽量放到更大的level
// 如果[small, large]与0层有重叠，则直接返回0
// 如果与level + 1文件有重叠，或者与level + 2层文件重叠过大，则都不应该放入level
// + 1，直接返回level 返回的level 最大为2
int Version::PickLevelForMemTableOutput(const Slice& smallest_user_key, const Slice& largest_user_key) {
  // 默认放到level 0
  int level = 0;
  if (!OverlapInLevel(0, &smallest_user_key, &largest_user_key)) {
    //如果这个MemTable的key range和level 0的文件的range没有交集

    // Push to next level if there is no overlap in next level,
    // and the #bytes overlapping in the level after that are limited.
    InternalKey start(smallest_user_key, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey limit(largest_user_key, 0, static_cast<ValueType>(0));
    std::vector<FileMetaData*> overlaps;
    while (level < config::kMaxMemCompactLevel) {
      // 与level + 1(下一层)文件有交集，只能直接返回该层
      // 目的是为了保证下一层文件是有序的
      if (OverlapInLevel(level + 1, &smallest_user_key, &largest_user_key)) {
        break;
      }
      if (level + 2 < config::kNumLevels) {
        // 如果level + 2(下两层)的文件与key range有重叠的文件大小超过20M
        // 目的是避免放入level + 1层后，与level + 2 compact时文件过大
        //  Check that file does not overlap too many grandparent bytes.
        GetOverlappingInputs(level + 2, &start, &limit, &overlaps);
        const int64_t sum = TotalFileSize(overlaps);
        if (sum > MaxGrandParentOverlapBytes(vset_->options_)) {
          break;
        }
      }
      level++;
    }
  }
  return level;
}

// Store in "*inputs" all files in "level" that overlap [begin,end]
void Version::GetOverlappingInputs(int level, const InternalKey* begin,
                                   const InternalKey* end,
                                   std::vector<FileMetaData*>* inputs) {
  assert(level >= 0);
  assert(level < config::kNumLevels);

  // clear all files in inputs
  inputs->clear();

  // get user key
  Slice user_begin, user_end;
  if (begin != nullptr) {
    user_begin = begin->user_key();
  }
  if (end != nullptr) {
    user_end = end->user_key();
  }

  const Comparator* user_cmp = vset_->icmp_.user_comparator();
  for (size_t i = 0; i < leveling_files_[level].size();) {

    // iterate all files in the specific level
    FileMetaData* f = leveling_files_[level][i++];
    const Slice file_start = f->smallest.user_key();
    const Slice file_limit = f->largest.user_key();

    // key space:
    // |-----------------------------|------------------------------------|-----------------------------|
    //           user_begin                       file_start                  file_limit
    //                               <----------------f------------------>
    if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
      // situation 1: f is completely before specified range; skip it
      // |-------------------|----------------|--------------------------------|----------------------------|
      //         file_start       file_limit           user_begin                      user_end
      //       <------f------>
    } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
      // situation 2: "f" is completely after specified range; skip it
      // |-----------------------------------|----------------------------|-----------------|----------|
      //               user_begin                    user_end          file_start    file_limit
      //                                                                 <-----f----->
    } else {
      inputs->push_back(f);
      if (level == 0) {
        // Level-0 files may overlap each other.  So check if the newly
        // added file has expanded the range.  If so, restart search.
        if (begin != nullptr && user_cmp->Compare(file_start, user_begin) < 0) {
          user_begin = file_start;
          inputs->clear();
          i = 0;
        } else if (end != nullptr &&
                   user_cmp->Compare(file_limit, user_end) > 0) {
          user_end = file_limit;
          inputs->clear();
          i = 0;
        }
      }
    }

  }
}




// Store in "*inputs" all files in "level" that overlap [begin,end]
void Version::GetOverlappingInputs(int level, const InternalKey* begin,
                                   const InternalKey* end,
                                   std::vector<FileMetaData*>* inputs, uint64_t partition_number) {
  assert(level >= 0);
  assert(level < config::kNumLevels);

  // clear all files in inputs
  inputs->clear();

  // get user key
  Slice user_begin, user_end;
  if (begin != nullptr) {
    user_begin = begin->user_key();
  }
  if (end != nullptr) {
    user_end = end->user_key();
  }

  const Comparator* user_cmp = vset_->icmp_.user_comparator();
  for (size_t i = 0; i < partitioning_leveling_files_[level][partition_number].size();) {

    // iterate all files in the specific level
    FileMetaData* f = partitioning_leveling_files_[level][partition_number][i++];
    const Slice file_start = f->smallest.user_key();
    const Slice file_limit = f->largest.user_key();

    // key space:
    // |-----------------------------|------------------------------------|-----------------------------|
    //           user_begin                       file_start                  file_limit
    //                               <----------------f------------------>
    if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
      // situation 1: f is completely before specified range; skip it
      // |-------------------|----------------|--------------------------------|----------------------------|
      //         file_start       file_limit           user_begin                      user_end
      //       <------f------>
    } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
      // situation 2: "f" is completely after specified range; skip it
      // |-----------------------------------|----------------------------|-----------------|----------|
      //               user_begin                    user_end          file_start    file_limit
      //                                                                 <-----f----->
    } else {
      inputs->push_back(f);
      if (level == 0 || level == 1) {
        // Level-0 files may overlap each other.  So check if the newly
        // added file has expanded the range.  If so, restart search.
        if (begin != nullptr && user_cmp->Compare(file_start, user_begin) < 0) {
          user_begin = file_start;
          inputs->clear();
          i = 0;
        } else if (end != nullptr &&
                   user_cmp->Compare(file_limit, user_end) > 0) {
          user_end = file_limit;
          inputs->clear();
          i = 0;
        }
      }
    }

  }
}


// Store in "*inputs" all files in "level" that overlap [begin,end]
/**
 * @brief Store in "*inputs" all files in "level" that overlap [begin, end]
 *
 * This function scans through the specified level and stores all files that overlap
 * with the given range [begin, end] into the provided vector tier_inputs.
 *
 * @param level The level to scan for overlapping files.
 * @param begin The start of the range to check for overlaps. Can be nullptr.
 * @param end The end of the range to check for overlaps. Can be nullptr.
 * @param tier_inputs The vector to store the overlapping files.
 */
void Version::GetOverlappingInputsWithTier(int level, const InternalKey* begin,
                                   const InternalKey* end,
                                   std::vector<FileMetaData*>* tier_inputs,
                                   int& selected_run) {
  assert(level >= 0);
  assert(level < config::kNumLevels);

  // clear all files in inputs
  tier_inputs->clear();

  // get user key
  Slice user_begin, user_end;
  if (begin != nullptr) {
    user_begin = begin->user_key();
  }
  if (end != nullptr) {
    user_end = end->user_key();
  }

  const Comparator* user_cmp = vset_->icmp_.user_comparator();
  if(level == 0){
    for (size_t i = 0; i < tiering_runs_[level][0].size();) {
      // iterate all files in the specific level
      FileMetaData* f = tiering_runs_[level][0][i++];
      const Slice file_start = f->smallest.user_key();
      const Slice file_limit = f->largest.user_key();

      // key space:
      // |-----------------------------|------------------------------------|-----------------------------|
      //           user_begin                       file_start                  file_limit
      //                               <----------------f------------------>
      if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
        // situation 1: f is completely before specified range; skip it
        // |-------------------|----------------|--------------------------------|----------------------------|
        //         file_start       file_limit           user_begin                      user_end
        //       <------f------>
      } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
        // situation 2: "f" is completely after specified range; skip it
        // |-----------------------------------|----------------------------|-----------------|----------|
        //               user_begin                    user_end          file_start    file_limit
        //                                                                 <-----f----->
      } else {
        tier_inputs->push_back(f);
        // Log(vset_->options_->info_log, "Added file %llu to tier inputs for level %d", static_cast<unsigned long long>(f->number), level);

        // Level-0 files may overlap each other.  So check if the newly
        // added file has expanded the range.  If so, restart search.
        if (begin != nullptr && user_cmp->Compare(file_start, user_begin) < 0) {
          // Log(vset_->options_->info_log, "File %llu expanded range on the left. Restarting search.", static_cast<unsigned long long>(f->number));
          user_begin = file_start;
          tier_inputs->clear();
          i = 0;
        } else if (end != nullptr && user_cmp->Compare(file_limit, user_end) > 0) {
          // Log(vset_->options_->info_log, "File %llu expanded range on the right. Restarting search.", static_cast<unsigned long long>(f->number));
          user_end = file_limit;
          tier_inputs->clear();
          i = 0;
        }
      }
    }
  }else{
    int num_overlapping_files = 0;
    for (int run = 0; run < config::kNum_SortedRuns; ++run) {
      if(tiering_runs_[level][run].empty()){
        selected_run = run;
        Log(vset_->options_->info_log, "Selected run %d for level %d because it is empty", selected_run, level);
        return ;
      }
    }
    for (int run = 0; run < config::kNum_SortedRuns; ++run) {
      int num_overlappping_files_in_this_run = 0;
      for (size_t i = 0; i < tiering_runs_[level][run].size();) {
        // iterate all files in the specific level
        FileMetaData* f = tiering_runs_[level][run][i++];
        const Slice file_start = f->smallest.user_key();
        const Slice file_limit = f->largest.user_key();

        // key space:
        // |-----------------------------|------------------------------------|-----------------------------|
        //           user_begin                       file_start                  file_limit
        //                               <----------------f------------------>
        if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
          // situation 1: f is completely before specified range; skip it
          // |-------------------|----------------|--------------------------------|----------------------------|
          //         file_start       file_limit           user_begin                      user_end
          //       <------f------>
        } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
          // situation 2: "f" is completely after specified range; skip it
          // |-----------------------------------|----------------------------|-----------------|----------|
          //               user_begin                    user_end          file_start    file_limit
          //                                                                 <-----f----->
        } else {
          num_overlappping_files_in_this_run++;
          // tier_inputs->push_back(f);
        }
      }
      // Log(vset_->options_->info_log, "Run %d has %d overlapping files for level %d", run, num_overlappping_files_in_this_run, level);
      if(num_overlappping_files_in_this_run > num_overlapping_files){
        num_overlapping_files = num_overlappping_files_in_this_run;
        selected_run = run;
        // Log(vset_->options_->info_log, "Selected run %d for level %d based on number of overlapping files", selected_run, level);
      }
    }

    Log(vset_->options_->info_log, "Selected run %d in level %d based on number of overlapping files as inputs[1]", selected_run, level);

    for (size_t i = 0; i < tiering_runs_[level][selected_run].size();) {
      // iterate all files in the specific level
      FileMetaData* f = tiering_runs_[level][selected_run][i++];
      const Slice file_start = f->smallest.user_key();
      const Slice file_limit = f->largest.user_key();

      if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
        // Log(vset_->options_->info_log, "Skipping file %llu: file_limit < user_begin", static_cast<unsigned long long>(f->number));
      } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
        // Log(vset_->options_->info_log, "Skipping file %llu: file_start > user_end", static_cast<unsigned long long>(f->number));
      } else { 
        tier_inputs->push_back(f);
        // Log(vset_->options_->info_log, "Added file %llu to tier inputs for level %d", static_cast<unsigned long long>(f->number), level);
      }
    }
  }
}


/**
 * @brief Generate a string that provides a detailed description of the current version.
 *
 * This function creates a string that lists the files at each level, including both leveling and
 * tiering strategies. For tiering, it distinguishes between different sorted runs.
 *
 * @return A string containing the debug information for the current version.
 */
std::string Version::DebugString() const {
  std::string r;
  for (int level = 0; level < config::kNumLevels; level++) {
    // E.g.,
    //   --- level 1 ---
    //   17:123['a' .. 'd']
    //   20:43['e' .. 'g']
    r.append("--- level ");
    AppendNumberTo(&r, level);
    r.append(" ---\n");

    const std::vector<FileMetaData*>& files = leveling_files_[level];
    for (size_t i = 0; i < files.size(); i++) {
      r.push_back(' ');
      AppendNumberTo(&r, files[i]->number);
      r.push_back(':');
      AppendNumberTo(&r, files[i]->file_size);
      r.append("[");
      r.append(files[i]->smallest.DebugString());
      r.append(" .. ");
      r.append(files[i]->largest.DebugString());
      r.append("]\n");
    }

    // const std::vector<FileMetaData*>& tiering_logical_files = tiering_files_[level];
    //   r.append("  --- sorted run ");
    //   AppendNumberTo(&r, logical_file->run_number);
    //   r.append(" ---\n");
    //   for (const auto& file : logical_file->actual_files) {
    //     r.push_back(' ');
    //     AppendNumberTo(&r, file.number);
    //     r.push_back(':');
    //     AppendNumberTo(&r, file.file_size);
    //     r.append("[");
    //     r.append(file.smallest.DebugString());
    //     r.append(" .. ");
    //     r.append(file.largest.DebugString());
    //     r.append("]\n");
    //   }
  }
  return r;
}

// A helper class so we can efficiently apply a whole sequence
// of edits to a particular state without creating intermediate
// Versions that contain full copies of the intermediate state.
class VersionSet::Builder {
 private:
  // Helper to sort by v->files_[file_number].smallest
  struct BySmallestKey {
    const InternalKeyComparator* internal_comparator;

    bool operator()(FileMetaData* f1, FileMetaData* f2) const {
      int r = internal_comparator->Compare(f1->smallest, f2->smallest);
      if (r != 0) {
        return (r < 0);
      } else {
        // Break ties by file number
        return (f1->number < f2->number);
      }
    }
  };

  typedef std::set<FileMetaData*, BySmallestKey> FileSet;
  struct LevelState {
    std::set<uint64_t> deleted_files;
    FileSet* added_files;
  };

  typedef std::set<FileMetaData*, BySmallestKey> PartitionFileSet;
  struct PartitionLevelState {
    std::map<uint64_t,std::set<uint64_t>> partition_deleted_files;
    std::map<uint64_t,PartitionFileSet*> partition_added_files;
  };

  typedef std::set<FileMetaData*, BySmallestKey> Tiering_files_in_run;
  struct TieringLevelState {
    std::map<int, Tiering_files_in_run*> added_files_every_runs;
    std::map<int,std::set<uint64_t>> deleted_tiering_files;
  };

  VersionSet* vset_;
  Version* base_;
  PartitionLevelState partition_levels_[config::kNumLevels];
  TieringLevelState tiering_levels_[config::kNumLevels];

 public:
  // Initialize a builder with the files from *base and other info from *vset
  Builder(VersionSet* vset, Version* base) : vset_(vset), base_(base) {
    base_->Ref();
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;

    // for (int level = 0; level < config::kNumLevels; level++) {
    //   levels_[level].added_files = new FileSet(cmp);
    // }

    for (int level = 0; level < config::kNumLevels; level++) {
      partition_levels_[level].partition_deleted_files.clear();
      if(level == 0){
        tiering_levels_[level].added_files_every_runs[0] = new Tiering_files_in_run(cmp);
      }else{
        for (int run = 0; run < config::kNum_SortedRuns; run++){
          tiering_levels_[level].added_files_every_runs[run] = new Tiering_files_in_run(cmp);
        }
      }
    }
  }

  ~Builder() {
    for (int level = 0; level < config::kNumLevels; level++) {
      auto& partition_files_map = partition_levels_[level].partition_added_files;

      for (auto& partition_pair : partition_files_map) {

        const PartitionFileSet* partition_file_set = partition_pair.second;
        std::vector<FileMetaData*> to_unref;
        to_unref.reserve(partition_file_set->size());
        for (PartitionFileSet::const_iterator it = partition_file_set->begin(); it != partition_file_set->end();
            ++it) {
          to_unref.push_back(*it);
        }

        delete partition_file_set;

        for (uint32_t i = 0; i < to_unref.size(); i++) {
          FileMetaData* f = to_unref[i];
          f->refs--;
          if (f->refs <= 0) {
            delete f;
          }
        }
      }
      
      for (const auto& run_entry : tiering_levels_[level].added_files_every_runs) {
        Tiering_files_in_run* added_tiering = run_entry.second;
        std::vector<FileMetaData*> to_unref;
        to_unref.reserve(added_tiering->size());
        for (Tiering_files_in_run::const_iterator it = added_tiering->begin(); it != added_tiering->end(); ++it) {
          to_unref.push_back(*it);
        }
        delete added_tiering;
        for (uint32_t i = 0; i < to_unref.size(); i++) {
          FileMetaData* f = to_unref[i];
          f->refs--;
          if (f->refs <= 0) {
            delete f;
          }
        }
      }
    }
    base_->Unref();
  }

  void EnsurePartitionFileSetExists(int level, uint64_t partition) {
    if (partition_levels_[level].partition_added_files.find(partition) == partition_levels_[level].partition_added_files.end()) {
      // 动态创建 PartitionFileSet 并添加到 partition_added_files 中
      BySmallestKey cmp;
      cmp.internal_comparator = &vset_->icmp_;
      partition_levels_[level].partition_added_files[partition] = new PartitionFileSet(cmp);
    }
  }


  // Apply all of the edits in *edit to the current state.
  void Apply(const VersionEdit* edit) {

    // Update compaction pointers
    for (size_t i = 0; i < edit->compact_pointers_.size(); i++) {
      const int level = std::get<0>(edit->compact_pointers_[i]);  
      const uint64_t partition = std::get<1>(edit->compact_pointers_[i]);
      const InternalKey key = std::get<2>(edit->compact_pointers_[i]);
      vset_->compact_pointer_[level][partition] = key.Encode().ToString();
    }

    // Delete files
    
    for (const auto& deleted_file_set_kvp : edit->deleted_partitioning_files_) {
      const int level = std::get<0>(deleted_file_set_kvp);
      const uint64_t partition = std::get<1>(deleted_file_set_kvp);
      const uint64_t number = std::get<2>(deleted_file_set_kvp);
      partition_levels_[level].partition_deleted_files[partition].insert(number);
      // DebugPrintPartitionDeletedFiles(level, partition);
      Log(vset_->options_->leveling_info_log, "Apply: Deleting file at level=%d,partition=%lu, number=%lu now deleted files: %lu", 
            level,partition, number, partition_levels_[level].partition_deleted_files[partition].size());
    }

    // Add new files
    // Log(vset_->options_->leveling_info_log, "{Apply}: Adding %lu new files", edit->new_partitioning_files_.size());
    for (size_t i = 0; i < edit->new_partitioning_files_.size(); i++) {

      const int level = std::get<0>(edit->new_partitioning_files_[i]);
      const uint64_t partition = std::get<1>(edit->new_partitioning_files_[i]);
      FileMetaData* f = new FileMetaData(std::get<2>(edit->new_partitioning_files_[i]));
      f->refs = 1;

      // We arrange to automatically compact this file after
      // a certain number of seeks.  Let's assume:
      //   (1) One seek costs 10ms
      //   (2) Writing or reading 1MB costs 10ms (100MB/s)
      //   (3) A compaction of 1MB does 25MB of IO:
      //         1MB read from this level
      //         10-12MB read from next level (boundaries may be misaligned)
      //         10-12MB written to next level
      // This implies that 25 seeks cost the same as the compaction
      // of 1MB of data.  I.e., one seek costs approximately the
      // same as the compaction of 40KB of data.  We are a little
      // conservative and allow approximately one seek for every 16KB
      // of data before triggering a compaction.
      f->allowed_seeks = static_cast<int>((f->file_size / 16384U));
      if (f->allowed_seeks < 100) f->allowed_seeks = 100;

        auto& deleted_files = partition_levels_[level].partition_deleted_files[partition];
        deleted_files.erase(f->number);
        
        // Check if the partition has no more deleted files and remove it
        if (deleted_files.empty()) {
          partition_levels_[level].partition_deleted_files.erase(partition);
        }

      // 确保 partition_added_files 已存在
      EnsurePartitionFileSetExists(level, partition);
      partition_levels_[level].partition_added_files[partition]->insert(f);
      Log(vset_->options_->leveling_info_log, "Apply: Adding new file at level=%d,partition=%lu, number=%lu, size=%lu added_files:%lu", 
        level, partition,f->number, f->file_size,partition_levels_[level].partition_added_files[partition]->size() );
    }
  }

  // Save the current state in *v.
  void SaveTo(Version* v) {
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;

    for (int level = 0; level < config::kNumLevels; level++) {

      // Merge the set of added files with the set of pre-existing files.
      // Drop any deleted files.  Store the result in *v.

      for (const auto& partition_pair : partition_levels_[level].partition_added_files) {
        uint64_t partition_id = partition_pair.first;
        const PartitionFileSet* added_files = partition_pair.second;
        // Log(vset_->options_->leveling_info_log, "partition_id %lu partition_pair.second %lu", 
        //       partition_id, partition_pair.second->size());
        int base_files_count = 0;
        int added_files_count = 0;
        int remaining_base_files_count = 0;

        if (base_->partitioning_leveling_files_[level].find(partition_id) != base_->partitioning_leveling_files_[level].end()) {
          const std::vector<FileMetaData*>& base_files = base_->partitioning_leveling_files_[level][partition_id];
          std::vector<FileMetaData*>::const_iterator base_iter = base_files.begin();
          std::vector<FileMetaData*>::const_iterator base_end = base_files.end();

          v->partitioning_leveling_files_[level][partition_id].reserve(base_files.size()+ added_files->size());
          Log(vset_->options_->leveling_info_log, "Merging files for partition %lu at level %d base_files.size():%lu added_files->size():%lu", 
              partition_id, level,base_files.size(), added_files->size());

          for (const auto& added_file : *added_files) {
            // Add all smaller files listed in base_
            for (std::vector<FileMetaData*>::const_iterator bpos = std::upper_bound(base_iter, base_end, added_file, cmp); base_iter != bpos; ++base_iter) {
                MaybeAddFile(v, level, partition_id, *base_iter);
                // Log(vset_->options_->info_log, "Adding base file: %p, partition: %lu, refs: %d", *base_iter, partition_id, (*base_iter)->refs);
                base_files_count++;
            }
            MaybeAddFile(v, level, partition_id, added_file);
            // Log(vset_->options_->info_log, "Adding added file: %p, partition: %lu, refs: %d", added_file, partition_id, added_file->refs);
            added_files_count++;
          }
          // Add remaining base files
          for (; base_iter != base_end; ++base_iter) {
            MaybeAddFile(v, level, partition_id, *base_iter);
            // Log(vset_->options_->info_log, "Adding remaining base file: %p, partition: %lu, refs: %d", *base_iter, partition_id, (*base_iter)->refs);
              remaining_base_files_count++;
          }
        }else{
          // Log(vset_->options_->leveling_info_log, "Adding new files for partition %lu at level %d", partition_id, level);
          for (FileMetaData* file : *added_files) {
            v->partitioning_leveling_files_[level][partition_id].push_back(file);
            // Log(vset_->options_->info_log, "FileMetaData* file: %p", file);
          }
          for(int i = 0; i<v->partitioning_leveling_files_[level][partition_id].size();i++){
            v->partitioning_leveling_files_[level][partition_id][i]->refs++;
            // Log(vset_->options_->info_log, "FileMetaData* v->partitioning_leveling_files_[level][partition_id][i]: %p", v->partitioning_leveling_files_[level][partition_id][i]);
          }
        }
        // Log(vset_->options_->leveling_info_log, "Partition %lu at level %d: base_files_count=%d, added_files_count=%d, remaining_base_files_count=%d",
        // partition_id, level, base_files_count, added_files_count, remaining_base_files_count);
      }
      
      // Handle partitions that have only deletions
      
      for (const auto& deleted_partition : partition_levels_[level].partition_deleted_files) {
        uint64_t partition_id = deleted_partition.first;
        const std::set<uint64_t>& deleted_files = deleted_partition.second;
        // Log(vset_->options_->leveling_info_log, "Partition %lu at level %d: deleted_partition.second.size()=%lu deleted_files.size()=%lu",
        // partition_id, level, deleted_partition.second.size(), deleted_files.size());
        if(!deleted_files.empty() && partition_levels_[level].partition_added_files.find(partition_id) == partition_levels_[level].partition_added_files.end()){
          Log(vset_->options_->leveling_info_log, "Processing deleted %lu files for partition %lu at level %d", deleted_files.size(), partition_id, level);
          const std::vector<FileMetaData*>& base_files = base_->partitioning_leveling_files_[level][partition_id];
          for (const auto& base_file : base_files) {
            MaybeAddFile(v, level, partition_id, base_file);
          }
        }
      }
      
      
      // Handle partitions that are in the base but not in added or deleted lists
      for (const auto& base_partition : base_->partitioning_leveling_files_[level]) {
        uint64_t partition_id = base_partition.first;
        // 如果 partition_added_files 中没有该分区，直接复制基础版本中的文件
        if (partition_levels_[level].partition_added_files.find(partition_id) == partition_levels_[level].partition_added_files.end()
              && partition_levels_[level].partition_deleted_files.find(partition_id) == partition_levels_[level].partition_deleted_files.end()){
          Log(vset_->options_->leveling_info_log, "Copying base files for partition %lu at level %d", partition_id, level);
          v->partitioning_leveling_files_[level][partition_id] = base_partition.second;
          for(int i = 0; i<v->partitioning_leveling_files_[level][partition_id].size();i++){
            v->partitioning_leveling_files_[level][partition_id][i]->refs++;
          }
        }
      }

      if(level == 0){
        v->tiering_runs_[level][0] = base_->tiering_runs_[level][0];
        for(int i=0;i<v->tiering_runs_[level][0].size();i++){
          v->tiering_runs_[level][0][i]->refs++;
        }
        continue;
      }else{
        // Handle tiering files
        for(int run=0; run < config::kNum_SortedRuns; run++){
          v->tiering_runs_[level][run] = base_->tiering_runs_[level][run];
          for(int i=0;i<v->tiering_runs_[level][run].size();i++){
            v->tiering_runs_[level][run][i]->refs++;
          }
        }
      }
#ifndef NDEBUG
      // Make sure there is no overlap in levels > 0
      if (level > 0) {
        for (uint32_t i = 1; i < v->leveling_files_[level].size(); i++) {
          const InternalKey& prev_end = v->leveling_files_[level][i - 1]->largest;
          const InternalKey& this_begin = v->leveling_files_[level][i]->smallest;
          if (vset_->icmp_.Compare(prev_end, this_begin) >= 0) {
            std::fprintf(stderr, "overlapping ranges in same level %s vs. %s\n",
                         prev_end.DebugString().c_str(),
                         this_begin.DebugString().c_str());
            std::abort();
          }
        }
      }
#endif
    }
  }

  // 添加调试输出的函数
  void DebugPrintPartitionDeletedFiles(int level, uint64_t partition) {
    auto& partition_deleted_files = partition_levels_[level].partition_deleted_files;

    if (partition_deleted_files.find(partition) != partition_deleted_files.end()) {
        const std::set<uint64_t>& deleted_files = partition_deleted_files[partition];
        if (!deleted_files.empty()) {
          fprintf(stderr, "Deleted files in partition %lu at level %d:\n", partition, level);
          for (const auto& file : deleted_files) {
            fprintf(stderr, "  File number: %lu\n", file);
          }
        } else {
          fprintf(stderr, "No deleted files in partition %lu at level %d (set is empty)\n", partition, level);
        }
    } else {
      fprintf(stderr, "No entry for partition %lu at level %d in partition_deleted_files\n", partition, level);
    }
  }


  void MaybeAddFile(Version* v, int level, uint64_t partition, FileMetaData* f) {
    // DebugPrintPartitionDeletedFiles(level, partition);
    // Log(vset_->options_->leveling_info_log, "Deleting files at level=%d,partition=%lu now deleted files: %lu", 
    //       level,partition, partition_levels_[level].partition_deleted_files[partition].size());
    if (partition_levels_[level].partition_deleted_files[partition].count(f->number) > 0) {
      // File is deleted: do nothing
      Log(vset_->options_->leveling_info_log, "File %lu in partition %lu at level %d is deleted, skipping", f->number, partition, level);
    } else {
      std::vector<FileMetaData*>* files = &v->partitioning_leveling_files_[level][partition];
      if (level > 1 && !files->empty()) {
        // Must not overlap
        assert(vset_->icmp_.Compare((*files)[files->size() - 1]->largest, f->smallest) < 0);
      }
      f->refs++;
      files->push_back(f);
      Log(vset_->options_->leveling_info_log, "Added file %lu to partition %lu at level %d, refs: %d", f->number, partition, level, f->refs);
    }
  }

  /**
  * Apply all of the edits in *edit to the current state for tiering files.
  *
  * @param edit The version edit to apply.
  * @param is_tiering Boolean flag indicating if the operation is for tiering files.
  */
  void Apply(const VersionEdit* edit, bool is_tiering) {
    assert(is_tiering == true);

    // Delete files
    for (const auto& deleted_file_set_kvp : edit->deleted_tiering_files_) {
      const int level = std::get<0>(deleted_file_set_kvp);
      const int run = std::get<1>(deleted_file_set_kvp);
      const uint64_t number = std::get<2>(deleted_file_set_kvp);
      tiering_levels_[level].deleted_tiering_files[run].insert(number);
    }

    // Add new files
    for (size_t i = 0; i < edit->new_tiering_files.size(); i++) {
      const int level = std::get<0>(edit->new_tiering_files[i]);
      const int run = std::get<1>(edit->new_tiering_files[i]);
      FileMetaData* f = new FileMetaData(std::get<2>(edit->new_tiering_files[i]));

      f->refs = 1;

      // We arrange to automatically compact this file after
      // a certain number of seeks.  Let's assume:
      //   (1) One seek costs 10ms
      //   (2) Writing or reading 1MB costs 10ms (100MB/s)
      //   (3) A compaction of 1MB does 25MB of IO:
      //         1MB read from this level
      //         10-12MB read from next level (boundaries may be misaligned)
      //         10-12MB written to next level
      // This implies that 25 seeks cost the same as the compaction
      // of 1MB of data.  I.e., one seek costs approximately the
      // same as the compaction of 40KB of data.  We are a little
      // conservative and allow approximately one seek for every 16KB
      // of data before triggering a compaction.
      f->allowed_seeks = static_cast<int>((f->file_size / 16384U));
      if (f->allowed_seeks < 100) f->allowed_seeks = 100;

      tiering_levels_[level].deleted_tiering_files[run].erase(f->number);
      tiering_levels_[level].added_files_every_runs[run]->insert(f);
      Log(vset_->options_->info_log, "Apply: Adding new tiering file at level=%d, run=%d, number=%lu, size=%lu", level,run, f->number, f->file_size);
    }
  }

  /**
  * @brief Save the current state in *v, specifically for tiering files.
  * 
  * @param v The Version to save the state to.
  * @param is_tiering A flag indicating if the operation is for tiering files.
  */
  void SaveTo(Version* v, bool is_tiering) {

    assert(is_tiering == true);

    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;

    for (int level = 0; level < config::kNumLevels; level++) {
      for (const auto& run_entry : base_->tiering_runs_[level]) {
        const Tiering_files_in_run* added_tier_files_this_run = tiering_levels_[level].added_files_every_runs[run_entry.first];
        const std::vector<FileMetaData*>& base_files = base_->tiering_runs_[level][run_entry.first];
        std::vector<FileMetaData*>::const_iterator base_iter = base_files.begin();
        std::vector<FileMetaData*>::const_iterator base_end = base_files.end();
        v->tiering_runs_[level][run_entry.first].reserve(base_files.size() + added_tier_files_this_run->size());
        int index = 0;
        for (const auto& added_file : *added_tier_files_this_run) {
          // Add all smaller files listed in base_
          for (std::vector<FileMetaData*>::const_iterator bpos = std::upper_bound(base_iter, base_end, added_file, cmp); base_iter != bpos; ++base_iter) {
            MaybeAddFile(v, level,run_entry.first, *base_iter);
            index++;
          }
          MaybeAddFile(v, level,run_entry.first, added_file);
          index++;
        }
        // Add remaining base files
        for (; base_iter != base_end; ++base_iter) {
          MaybeAddFile(v, level,run_entry.first, *base_iter);
        }

        vset_->tiering_compact_pointer_[level][run_entry.first] = -1;
        for(int i = 0; i<v->tiering_runs_[level][run_entry.first].size();i++){
          if(vset_->tiering_compact_pointer_[level][run_entry.first] == -1 || 
                v->tiering_runs_[level][run_entry.first][i]->number<vset_->tiering_compact_pointer_[level][run_entry.first] ){
            vset_->tiering_compact_pointer_[level][run_entry.first] = i;
          }
        }  
      }

      // Handle leveling files
      v->partitioning_leveling_files_[level] = base_->partitioning_leveling_files_[level];
      for (auto& partition_pair : v->partitioning_leveling_files_[level]) {
        std::vector<FileMetaData*>& files = partition_pair.second;
        for (size_t i = 0; i < files.size(); i++) {
          files[i]->refs++;
        }
      }

#ifndef NDEBUG
      // Make sure there is no overlap in levels > 0
      if (level > 0) {
        for(int run=0; run<config::kNum_SortedRuns; run++){
          for (uint32_t i = 1; i < v->tiering_runs_[level][run].size(); i++) {
            const InternalKey& prev_end = v->tiering_runs_[level][run][i - 1]->largest;
            const InternalKey& this_begin = v->tiering_runs_[level][run][i]->smallest;
            if (vset_->icmp_.Compare(prev_end, this_begin) >= 0) {
              std::fprintf(stderr, "overlapping ranges in same level %s vs. %s\n",
                          prev_end.DebugString().c_str(),
                          this_begin.DebugString().c_str());
              std::abort();
            }
          }
        }
      }
#endif
    }
  }
  
  /**
  * @brief Adds a file to the version if it is not marked for deletion.
  * 
  * @param v The Version to add the file to.
  * @param level The level at which the file exists.
  * @param run The run in which the file exists.
  * @param f The file metadata to add.
  */
  void MaybeAddFile(Version* v, int level, int run, FileMetaData* f) {
    if (tiering_levels_[level].deleted_tiering_files[run].count(f->number) > 0) {
      // File is deleted: do nothing
    } else {
      std::vector<FileMetaData*>* files = &v->tiering_runs_[level][run];
      if (level > 0 && !files->empty()) {
        // Must not overlap
        assert(vset_->icmp_.Compare((*files)[files->size() - 1]->largest, f->smallest) < 0);
      }
      f->refs++;
      files->push_back(f);
    }
  }

};

VersionSet::VersionSet(const std::string& dbname, const Options* options,
                       TableCache* table_cache,
                       const InternalKeyComparator* cmp)
    : env_(options->env),
      dbname_(dbname),
      options_(options),
      table_cache_(table_cache),
      icmp_(*cmp),
      next_file_number_(2), // 1 is the manifest file
      next_parition_number_(1), // used to identify partitions
      manifest_file_number_(0),  // Filled by Recover()
      last_sequence_(0),
      log_number_(0),
      prev_log_number_(0),
      descriptor_file_(nullptr),
      descriptor_log_(nullptr),
      dummy_versions_(this),
      current_(nullptr) {
  AppendVersion(new Version(this));
}

VersionSet::~VersionSet() {
  current_->Unref();
  assert(dummy_versions_.next_ == &dummy_versions_);  // List must be empty
  delete descriptor_log_;
  delete descriptor_file_;
}

void VersionSet::AppendVersion(Version* v) {
  // Make "v" current
  assert(v->refs_ == 0);
  assert(v != current_);
  if (current_ != nullptr) {
    current_->Unref();
  }
  current_ = v;
  v->Ref();

  // Append to linked list
  v->prev_ = dummy_versions_.prev_;
  v->next_ = &dummy_versions_;
  v->prev_->next_ = v;
  v->next_->prev_ = v;
}

Status VersionSet::LogAndApply(VersionEdit* edit, port::Mutex* mu) {
  if (edit->has_log_number_) {
    assert(edit->log_number_ >= log_number_);
    // 没有在处理前面的日志文件
    assert(edit->log_number_ < next_file_number_);
  } else {
    edit->SetLogNumber(log_number_);
  }

  if (!edit->has_prev_log_number_) {
    edit->SetPrevLogNumber(prev_log_number_);
  }

  edit->SetNextFile(next_file_number_);
  edit->SetLastSequence(last_sequence_);

  Version* v = new Version(this);
  {
    Builder builder(this, current_);
    bool is_tiering = edit->get_is_tiering();
    if(is_tiering){
      builder.Apply(edit, is_tiering);
      builder.SaveTo(v,is_tiering);
    }else{
      builder.Apply(edit);
      builder.SaveTo(v);
    }
  }
  Finalize(v);

  // Initialize new descriptor log file if necessary by creating
  // a temporary file that contains a snapshot of the current version.
  std::string new_manifest_file;
  Status s;

  if (descriptor_log_ == nullptr) {
    // No reason to unlock *mu here since we only hit this path in the
    // first call to LogAndApply (when opening the database).
    assert(descriptor_file_ == nullptr);
    new_manifest_file = DescriptorFileName(dbname_, manifest_file_number_);
    s = env_->NewWritableFile(new_manifest_file, &descriptor_file_);
    if (s.ok()) {
      descriptor_log_ = new log::Writer(descriptor_file_);
      s = WriteSnapshot(descriptor_log_);
    }
  }

  // Unlock during expensive MANIFEST log write
  {
    mu->Unlock();

    // Write new record to MANIFEST log
    if (s.ok()) {
      std::string record;
      edit->EncodeTo(&record);
      s = descriptor_log_->AddRecord(record);
      if (s.ok()) {
        s = descriptor_file_->Sync();
      }
      if (!s.ok()) {
        Log(options_->info_log, "MANIFEST write: %s\n", s.ToString().c_str());
      }
    }

    // If we just created a new descriptor file, install it by writing a
    // new CURRENT file that points to it.
    if (s.ok() && !new_manifest_file.empty()) {
      s = SetCurrentFile(env_, dbname_, manifest_file_number_);
    }

    mu->Lock();
  }

  // Install the new version
  if (s.ok()) {
    AppendVersion(v);
    log_number_ = edit->log_number_;
    prev_log_number_ = edit->prev_log_number_;
  } else {
    delete v;
    if (!new_manifest_file.empty()) {
      delete descriptor_log_;
      delete descriptor_file_;
      descriptor_log_ = nullptr;
      descriptor_file_ = nullptr;
      env_->RemoveFile(new_manifest_file);
    }
  }

  return s;
}

Status VersionSet::Recover(bool* save_manifest) {
  struct LogReporter : public log::Reader::Reporter {
    Status* status;
    void Corruption(size_t bytes, const Status& s) override {
      if (this->status->ok()) *this->status = s;
    }
  };

  // Read "CURRENT" file, which contains a pointer to the current manifest file
  std::string current;
  Status s = ReadFileToString(env_, CurrentFileName(dbname_), &current);
  if (!s.ok()) {
    return s;
  }
  if (current.empty() || current[current.size() - 1] != '\n') {
    return Status::Corruption("CURRENT file does not end with newline");
  }
  current.resize(current.size() - 1);

  std::string dscname = dbname_ + "/" + current;
  SequentialFile* file;
  s = env_->NewSequentialFile(dscname, &file);
  if (!s.ok()) {
    if (s.IsNotFound()) {
      return Status::Corruption("CURRENT points to a non-existent file",
                                s.ToString());
    }
    return s;
  }

  bool have_log_number = false;
  bool have_prev_log_number = false;
  bool have_next_file = false;
  bool have_last_sequence = false;
  uint64_t next_file = 0;
  uint64_t last_sequence = 0;
  uint64_t log_number = 0;
  uint64_t prev_log_number = 0;
  Builder builder(this, current_);
  int read_records = 0;

  {
    LogReporter reporter;
    reporter.status = &s;
    log::Reader reader(file, &reporter, true /*checksum*/,
                       0 /*initial_offset*/);
    Slice record;
    std::string scratch;
    while (reader.ReadRecord(&record, &scratch) && s.ok()) {
      ++read_records;
      VersionEdit edit;
      s = edit.DecodeFrom(record);
      if (s.ok()) {
        if (edit.has_comparator_ &&
            edit.comparator_ != icmp_.user_comparator()->Name()) {
          s = Status::InvalidArgument(
              edit.comparator_ + " does not match existing comparator ",
              icmp_.user_comparator()->Name());
        }
      }

      if (s.ok()) {
        builder.Apply(&edit);
      }

      if (edit.has_log_number_) {
        log_number = edit.log_number_;
        have_log_number = true;
      }

      if (edit.has_prev_log_number_) {
        prev_log_number = edit.prev_log_number_;
        have_prev_log_number = true;
      }

      if (edit.has_next_file_number_) {
        next_file = edit.next_file_number_;
        have_next_file = true;
      }

      if (edit.has_last_sequence_) {
        last_sequence = edit.last_sequence_;
        have_last_sequence = true;
      }
    }
  }
  delete file;
  file = nullptr;

  if (s.ok()) {
    if (!have_next_file) {
      s = Status::Corruption("no meta-nextfile entry in descriptor");
    } else if (!have_log_number) {
      s = Status::Corruption("no meta-lognumber entry in descriptor");
    } else if (!have_last_sequence) {
      s = Status::Corruption("no last-sequence-number entry in descriptor");
    }

    if (!have_prev_log_number) {
      prev_log_number = 0;
    }

    MarkFileNumberUsed(prev_log_number);
    MarkFileNumberUsed(log_number);
  }

  if (s.ok()) {
    Version* v = new Version(this);
    builder.SaveTo(v);
    // Install recovered version
    Finalize(v);
    AppendVersion(v);
    manifest_file_number_ = next_file;
    next_file_number_ = next_file + 1;
    last_sequence_ = last_sequence;
    log_number_ = log_number;
    prev_log_number_ = prev_log_number;

    // See if we can reuse the existing MANIFEST file.
    if (ReuseManifest(dscname, current)) {
      // No need to save new manifest
    } else {
      *save_manifest = true;
    }
  } else {
    std::string error = s.ToString();
    Log(options_->info_log, "Error recovering version set with %d records: %s",
        read_records, error.c_str());
  }

  return s;
}

bool VersionSet::ReuseManifest(const std::string& dscname,
                               const std::string& dscbase) {
  if (!options_->reuse_logs) {
    return false;
  }
  FileType manifest_type;
  uint64_t manifest_number;
  uint64_t manifest_size;
  if (!ParseFileName(dscbase, &manifest_number, &manifest_type) ||
      manifest_type != kDescriptorFile ||
      !env_->GetFileSize(dscname, &manifest_size).ok() ||
      // Make new compacted MANIFEST if old one is too big
      manifest_size >= TargetFileSize(options_)) {
    return false;
  }

  assert(descriptor_file_ == nullptr);
  assert(descriptor_log_ == nullptr);
  Status r = env_->NewAppendableFile(dscname, &descriptor_file_);
  if (!r.ok()) {
    Log(options_->info_log, "Reuse MANIFEST: %s\n", r.ToString().c_str());
    assert(descriptor_file_ == nullptr);
    return false;
  }

  Log(options_->info_log, "Reusing MANIFEST %s\n", dscname.c_str());
  descriptor_log_ = new log::Writer(descriptor_file_, manifest_size);
  manifest_file_number_ = manifest_number;
  return true;
}

void VersionSet::MarkFileNumberUsed(uint64_t number) {
  if (next_file_number_ <= number) {
    next_file_number_ = number + 1;
  }
}

void VersionSet::Finalize(Version* v) {

  // Precomputed best level for next compaction
  int best_tiering_level = -1;
  double best_tier_score = -1;

  double best_leveling_score = -1;

  std::map<uint64_t, double> best_scores;
  bool need_compaction_in_leveling = false;
  // for partitioning leveling
  for (int level = 0; level < config::kNumLevels - 1; level++) {

    for (const auto& partition : v->partitioning_leveling_files_[level]) {

      uint64_t partition_number = partition.first;
      double level_score = 0.0;

      if (level == 0) {
        level_score = partition.second.size() / static_cast<double>(config::kInitialPartitionLevelingCompactionTrigger);   
      }else if(level == 1){
        level_score = partition.second.size() / static_cast<double>(config::kPartitionLevelingL1CompactionTrigger); 
      }else{
        const uint64_t level_bytes = TotalFileSize(partition.second);  
        level_score = static_cast<double>(level_bytes) / (MaxBytesForLevel(options_, level) );
      }
      
      if(level_score >= 1.00){
        need_compaction_in_leveling = true;
        auto it = best_scores.find(partition_number);
        if (it == best_scores.end() || level_score > it->second) {
          best_scores[partition_number] = level_score;
          v->partitions_to_merge_[partition_number] = level;
        }
      }
    }
  }

  if(need_compaction_in_leveling){
    v->compaction_score_ = 1.0;
  }
  // 记录需要合并的 partition
  for (const auto& entry : v->partitions_to_merge_) {
    uint64_t partition_number = entry.first;
    int level = entry.second;
    size_t file_count = v->partitioning_leveling_files_[level][partition_number].size();
    uint64_t total_size = TotalFileSize(v->partitioning_leveling_files_[level][partition_number]);
    uint64_t max_capacity = MaxBytesForLevel(options_, level);
        Log(options_->leveling_info_log, 
        "Partition %lu needs compaction at level %d: "
        "File count = %zu, Total size = %llu, Max capacity = %llu",
        partition_number, level, file_count,
        static_cast<unsigned long long>(total_size),
        static_cast<unsigned long long>(max_capacity));
  }


  // for tiering part
  for (int level = 0; level < config::kNumLevels - 1; level++) {
    double tier_score;
    if (level == 0) {
      // We treat level-0 specially by bounding the number of files
      // instead of number of bytes for two reasons:
      //
      // (1) With larger write-buffer sizes, it is nice not to do too
      // many level-0 compactions.
      //
      // (2) The files in level-0 are merged on every read and
      // therefore we wish to avoid too many files when the individual
      // file size is small (perhaps because of a small write-buffer
      // setting, or very high compression ratios, or lots of
      // overwrites/deletions).
      tier_score = v->NumTieringFiles(0) /
              static_cast<double>(config::kTiering_and_leveling_Multiplier);
    } else {
      // Compute the ratio of current size to size limit.
      const uint64_t tiering_bytes = TotalFileSize(v->tiering_runs_[level]);
      tier_score = static_cast<double>(tiering_bytes) / (TieringSpaceConfig::kTieringLevelSizes[level]);
    }

    if ( tier_score > best_tier_score) {
      best_tiering_level = level;
      best_tier_score = tier_score;
    }
  }


  v->tiering_compaction_level_ = best_tiering_level;
  v->tieirng_compaction_score_ = best_tier_score;
  Log(options_->info_log, "Finalize:  Best tiering level = %d, Best tiering score = %f", best_tiering_level, best_tier_score);
}

Status VersionSet::WriteSnapshot(log::Writer* log) {
  // TODO: Break up into multiple records to reduce memory usage on recovery?

  // Save metadata
  VersionEdit edit;
  edit.SetComparatorName(icmp_.user_comparator()->Name());

  // Save compaction pointers
  for (int level = 0; level < config::kNumLevels; level++) {
    for (const auto& entry : compact_pointer_[level]) {
      uint64_t partition_number = entry.first;
      const std::string& compact_pointer = entry.second;
      if (!compact_pointer_[level].empty()) {
        InternalKey key;
        key.DecodeFrom(compact_pointer);
        edit.SetCompactPointer(level,partition_number, key);
      }
    }
  }

  // Save files
  for (int level = 0; level < config::kNumLevels; level++) {
    // const std::vector<FileMetaData*>& files = current_->leveling_files_[level];
    const auto& partitions = current_->partitioning_leveling_files_[level];
    for (const auto& partition_entry : partitions) {
      uint64_t partition_number = partition_entry.first;
      const std::vector<FileMetaData*>& files = partition_entry.second;
      for (size_t i = 0; i < files.size(); i++) {
        const FileMetaData* f = files[i];
        edit.AddPartitionLevelingFile(partition_number, level, f->number, f->file_size, f->smallest, f->largest);
      }
    }
  }

  std::string record;
  edit.EncodeTo(&record);
  return log->AddRecord(record);
}


// ==== Start of modified code ====

/**
 * @brief Get the total number of files at the specified level.
 *
 * This function calculates the total number of files by summing up the number
 * of leveling and tiering files at the given level.
 *
 * @param level The level to query.
 * @return The total number of files at the specified level.
 */
int VersionSet::NumLevelFiles(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return Num_Level_Partitionleveling_Files(level)+Num_Level_tiering_Files(level);
}


/**
 * @brief Get the number of files using tiering strategy at the specified level.
 *
 * This function iterates through the tiering_logical_files_ at the given level and
 * counts the number of actual files contained in each logical file metadata
 * that uses the tiering strategy (run_number != 0).
 *
 * @param level The level to query.
 * @return The number of files using tiering strategy at the specified level.
 */
int VersionSet::Num_Level_tiering_Files(int level, int run) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->tiering_runs_[level][run].size();
}

int VersionSet::Num_Level_tiering_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  int total_files = 0;

  for (const auto& partition : current_->tiering_runs_[level]) {
    total_files += partition.second.size();
  }
  return total_files;

}

/**
 * @brief Get the number of files using leveling strategy at the specified level.
 *
 * This function directly returns the size of the leveling_files_ vector at the
 * given level, as each element in the vector represents a file using the leveling strategy.
 *
 * @param level The level to query.
 * @return The number of files using leveling strategy at the specified level.
 */
int VersionSet::Num_Level_leveling_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->leveling_files_[level].size();
}

int VersionSet::Num_Level_Partitionleveling_Files(int level,uint64_t part) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->partitioning_leveling_files_[level][part].size();
}

int VersionSet::Num_Level_maxPartitionleveling_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  int max_files = 0;

  for (const auto& partition : current_->partitioning_leveling_files_[level]) {
    if(partition.second.size() > max_files){
      max_files = partition.second.size();
    }
  }
  return max_files;
}

int VersionSet::Num_Level_Partitionleveling_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  
  int total_files = 0;
  for (const auto& partition : current_->partitioning_leveling_files_[level]) {
    total_files += partition.second.size();
  }
  return total_files;
}
// ==== End of modified code ====


// const char* VersionSet::LevelSummary(LevelSummaryStorage* scratch) const {
//   // Update code if kNumLevels changes
//   static_assert(config::kNumLevels == 7, "");
//   std::snprintf(
//       scratch->buffer, sizeof(scratch->buffer), "files[ %d %d %d %d %d %d %d ]",
//       int(current_->files_[0].size()), int(current_->files_[1].size()),
//       int(current_->files_[2].size()), int(current_->files_[3].size()),
//       int(current_->files_[4].size()), int(current_->files_[5].size()),
//       int(current_->files_[6].size()));
//   return scratch->buffer;
// }

/**
 * @brief Generate a summary of the number of files in each level.
 *
 * This function creates a summary string that lists the number of leveling and
 * tiering files at each level, and stores it in the provided scratch buffer.
 *
 * @param scratch A pointer to the LevelSummaryStorage where the summary string will be stored.
 * @return A pointer to the summary string.
 */
const char* VersionSet::LevelSummary(LevelSummaryStorage* scratch) const {
  static_assert(config::kNumLevels == 7, "");
  int leveling_file_counts[config::kNumLevels] = {0};

  // Calculate the number of leveling files at each level
  for (int level = 0; level < config::kNumLevels; ++level) {
    leveling_file_counts[level] = current_->leveling_files_[level].size();
  }

  std::string summary = "leveling files [ ";
  for (int i = 0; i < config::kNumLevels; ++i) {
    summary += std::to_string(leveling_file_counts[i]) + " ";
  }
  summary += "], tiering files [";

  // Calculate the number of actual tiering files at each level
  for (int level = 0; level < config::kNumLevels; ++level) {
    summary += "level " + std::to_string(level) + ": { ";
    for (const auto& run : current_->tiering_runs_[level]) {
      summary += "run " + std::to_string(run.first) + ": " + std::to_string(run.second.size()) + " files, ";
    }
    summary += "}, ";
  }
  summary += "]";

  std::snprintf(scratch->buffer, sizeof(scratch->buffer), "%s", summary.c_str());
  return scratch->buffer;
}

void VersionSet::init_tieirng_compaction_pointer(){
  for (int level = 0; level < config::kNumLevels; ++level) {
    tiering_compact_pointer_[level].clear();
    if(level == 0){
      tiering_compact_pointer_[level][0] = -1;
      continue;
    }
    for (int run = 0; run < config::kNum_SortedRuns; ++run) {
      tiering_compact_pointer_[level][run] = -1;
    }
  }
}

void VersionSet::set_overlap_range(const std::vector<FileMetaData*>& inputs1,
                 const std::vector<FileMetaData*>& inputs2) {
  overlap_ranges.clear();

  for (const auto& file1 : inputs1) 
  {
    for (const auto& file2 : inputs2) 
    {
    // check if file1 and file2 have overlapping key ranges
      if (file1->largest.user_key().compare(file2->smallest.user_key()) >= 0 &&
        file2->largest.user_key().compare(file1->smallest.user_key()) >= 0) {
        if (icmp_.user_comparator()->Compare(file1->largest.user_key(), file2->smallest.user_key()) >= 0 &&
                icmp_.user_comparator()->Compare(file2->largest.user_key(), file1->smallest.user_key()) >= 0) 
        {
          int startCompare = icmp_.user_comparator()->Compare(file1->smallest.user_key(), file2->smallest.user_key());
          int endCompare = icmp_.user_comparator()->Compare(file1->largest.user_key(), file2->largest.user_key());
          Slice overlap_start = startCompare < 0 ? file2->smallest.user_key() : file1->smallest.user_key();
          Slice overlap_end = endCompare > 0 ? file2->largest.user_key() : file1->largest.user_key();
                
          // adding the overlapping range to the list
          overlap_ranges.push_back(std::make_pair(overlap_start, overlap_end));
        }
      }
    }
  }
}

bool VersionSet::compute_hot_cold_range(const Slice& key, const std::pair<Slice, Slice>& hot_range, bool& is_hot) {
    
    bool in_overlap = false;
    for (const auto& range : overlap_ranges) {
        if (icmp_.user_comparator()->Compare(key, range.first) >= 0 &&
            icmp_.user_comparator()->Compare(key, range.second) <= 0) {
            in_overlap = true;
            break;
        }
    }
   
    // if (!in_overlap) {
    //     return false;
    // }

    // // 接下来判断key是否在hot range内
    // if (icmp_.user_comparator()->Compare(key, hot_range.first) >= 0 &&
    //     icmp_.user_comparator()->Compare(key, hot_range.second) <= 0) {
    //     // key在hot range内
    //     is_hot = true; // 表示是hot key
    // } else {
    //     // key is not in hot range,key is a cold key 
    //     is_hot = false;
    // }

    return in_overlap;
}


uint64_t VersionSet::ApproximateOffsetOf(Version* v, const InternalKey& ikey) {
  uint64_t result = 0;
  for (int level = 0; level < config::kNumLevels; level++) {
    const std::vector<FileMetaData*>& files = v->leveling_files_[level];
    for (size_t i = 0; i < files.size(); i++) {
      if (icmp_.Compare(files[i]->largest, ikey) <= 0) {
        // Entire file is before "ikey", so just add the file size
        result += files[i]->file_size;
      } else if (icmp_.Compare(files[i]->smallest, ikey) > 0) {
        // Entire file is after "ikey", so ignore
        if (level > 0) {
          // Files other than level 0 are sorted by meta->smallest, so
          // no further files in this level will contain data for
          // "ikey".
          break;
        }
      } else {
        // "ikey" falls in the range for this table.  Add the
        // approximate offset of "ikey" within the table.
        Table* tableptr;
        Iterator* iter = table_cache_->NewIterator(
            ReadOptions(), files[i]->number, files[i]->file_size, &tableptr);
        if (tableptr != nullptr) {
          result += tableptr->ApproximateOffsetOf(ikey.Encode());
        }
        delete iter;
      }
    }
  }
  return result;
}


/**
 * @brief Add all live files to the specified set.
 *
 * This function iterates through all versions and levels, collecting the
 * file numbers of all live files from both tiering and leveling strategies,
 * and inserting them into the provided set. It is used to keep track of files
 * that are still in use and should not be deleted.
 *
 * @param live A pointer to a set that will be populated with the numbers
 *             of all live files.
 */
void VersionSet::AddLiveFiles(std::set<uint64_t>* live, bool is_leveling) {
  for (Version* v = dummy_versions_.next_; v != &dummy_versions_; v = v->next_) {
    for (int level = 0; level < config::kNumLevels; level++) {
      
      if(is_leveling){
        Log(options_->leveling_info_log, "Version %p", static_cast<void*>(v));
      }else{
        Log(options_->info_log, "Version %p", static_cast<void*>(v));
      }

      for (const auto& partition_pair : v->partitioning_leveling_files_[level]) {
        uint64_t partition_id = partition_pair.first;
        const std::vector<FileMetaData*>& leveling_files = partition_pair.second;
        if(is_leveling){
          Log(options_->leveling_info_log, "  Level %d, Partition %llu: %zu files", level, (unsigned long long)partition_id, leveling_files.size());
        }    
        for (size_t i = 0; i < leveling_files.size(); i++) {
          live->insert(leveling_files[i]->number);
          // Log(options_->info_log, "    Leveling File #%llu, size: %llu",
          // (unsigned long long)leveling_files[i]->number,
          // (unsigned long long)leveling_files[i]->file_size);
        }
      }

      // Process tiering files in each run
      for (const auto& run_pair : v->tiering_runs_[level]) {
        int run = run_pair.first;
        const std::vector<FileMetaData*>& tiering_files = run_pair.second;
        if(!is_leveling){
          Log(options_->info_log, "  Level %d (tiering, run %d): %zu files", level, run, tiering_files.size());
        } 
        for (size_t i = 0; i < tiering_files.size(); i++) {
          live->insert(tiering_files[i]->number);
          // Log(options_->info_log, "    Tiering File #%llu, size: %llu",
              // (unsigned long long)tiering_files[i]->number,
              // (unsigned long long)tiering_files[i]->file_size);
        }
      }
    }
  }
  Log(options_->leveling_info_log, "\n\n");
}



int64_t VersionSet::NumLevelBytes(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return TotalFileSize(current_->leveling_files_[level]);
}

int64_t VersionSet::MaxNextLevelOverlappingBytes() {
  int64_t result = 0;
  std::vector<FileMetaData*> overlaps;
  for (int level = 1; level < config::kNumLevels - 1; level++) {
    for (size_t i = 0; i < current_->leveling_files_[level].size(); i++) {
      const FileMetaData* f = current_->leveling_files_[level][i];
      current_->GetOverlappingInputs(level + 1, &f->smallest, &f->largest,
                                     &overlaps);
      const int64_t sum = TotalFileSize(overlaps);
      if (sum > result) {
        result = sum;
      }
    }
  }
  return result;
}

// Stores the minimal range that covers all entries in inputs in
// *smallest, *largest.
// REQUIRES: inputs is not empty
void VersionSet::GetRange(const std::vector<FileMetaData*>& inputs,
                          InternalKey* smallest, InternalKey* largest) {
  assert(!inputs.empty());
  smallest->Clear();
  largest->Clear();
  for (size_t i = 0; i < inputs.size(); i++) {
    FileMetaData* f = inputs[i];
    if (i == 0) {
      *smallest = f->smallest;
      *largest = f->largest;
    } else {
      if (icmp_.Compare(f->smallest, *smallest) < 0) {
        *smallest = f->smallest;
      }
      if (icmp_.Compare(f->largest, *largest) > 0) {
        *largest = f->largest;
      }
    }
  }
}

// Stores the minimal range that covers all entries in inputs1 and inputs2
// in *smallest, *largest.
// REQUIRES: inputs is not empty
void VersionSet::GetRange2(const std::vector<FileMetaData*>& inputs1,
                           const std::vector<FileMetaData*>& inputs2,
                           InternalKey* smallest, InternalKey* largest) {
  std::vector<FileMetaData*> all = inputs1;
  all.insert(all.end(), inputs2.begin(), inputs2.end());
  GetRange(all, smallest, largest);
}

Iterator* VersionSet::MakeInputIterator(Compaction* c) {
  ReadOptions options;
  options.verify_checksums = options_->paranoid_checks;
  options.fill_cache = false;

  // Level-0 files have to be merged together.  For other levels,
  // we will make a concatenating iterator per level.
  // TODO(opt): use concatenating iterator for level-0 if there is no overlap
  const int space = (c->level() == 0 ? c->inputs_[0].size() + c->inputs_[1].size() : (c->level() == 1 ? c->inputs_[0].size() + 1 : 2));
  Iterator** list = new Iterator*[space];
  int num = 0;
  for (int which = 0; which < 2; which++) {
    if (!c->inputs_[which].empty()) {
      if (c->level() + which <= 1) {
        const std::vector<FileMetaData*>& files = c->inputs_[which];
        for (size_t i = 0; i < files.size(); i++) {
          list[num++] = table_cache_->NewIterator(options, files[i]->number,
                                                  files[i]->file_size);
        }
      } else {
        // Create concatenating iterator for the files from this level
        list[num++] = NewTwoLevelIterator(
            new Version::LevelFileNumIterator(icmp_, &c->inputs_[which]),
            &GetFileIterator, table_cache_, options);
      }
    }
  }
  assert(num <= space);
  Iterator* result = NewMergingIterator(&icmp_, list, num);
  delete[] list;
  return result;
}

Iterator* VersionSet::MakeTieringInputIterator(TieringCompaction* c) {
  ReadOptions options;
  options.verify_checksums = options_->paranoid_checks;
  options.fill_cache = false;

  // Level-0 files have to be merged together.  For other levels,
  // we will make a concatenating iterator per level.
  // TODO(opt): use concatenating iterator for level-0 if there is no overlap
  const int space = (c->level() == 0 ? c->tiering_inputs_[0].size() + 1 : 2);
  Iterator** list = new Iterator*[space];
  int num = 0;
  for (int which = 0; which < 2; which++) {
    if (!c->tiering_inputs_[which].empty()) {
      if (c->level() + which == 0) {
        const std::vector<FileMetaData*>& files = c->tiering_inputs_[which];
        for (size_t i = 0; i < files.size(); i++) {
          list[num++] = table_cache_->NewIterator(options, files[i]->number,
                                                  files[i]->file_size);
        }
      } else {
        // Create concatenating iterator for the files from this level
        list[num++] = NewTwoLevelIterator(
            new Version::LevelFileNumIterator(icmp_, &c->tiering_inputs_[which]),
            &GetFileIterator, table_cache_, options);
      }
    }
  }
  assert(num <= space);
  Iterator* result = NewMergingIterator(&icmp_, list, num);
  delete[] list;
  return result;
}


void VersionSet::UpdateCompactionSpaces() {
  for (int i = 0; i < config::kNumLevels; i++) {
    double leveling_ratio = CompactionConfig::adaptive_compaction_configs[i]->levling_ratio;
    double tiering_ratio = CompactionConfig::adaptive_compaction_configs[i]->tieirng_ratio;

    CompactionConfig::adaptive_compaction_configs[i]->leveling_space = MaxBytesForLevel(options_, i) * leveling_ratio;
    CompactionConfig::adaptive_compaction_configs[i]->tiering_space = MaxBytesForLevel(options_, i) * tiering_ratio;

    Log(options_->info_log, "Level %d: leveling_ratio = %f, tiering_ratio = %f, leveling_space = %f, tiering_space = %f",
        i, leveling_ratio, tiering_ratio, CompactionConfig::adaptive_compaction_configs[i]->leveling_space, CompactionConfig::adaptive_compaction_configs[i]->tiering_space);
  }
}



bool VersionSet::DetermineLevel0Compaction(int& level) {

  assert(level == 0);
  int leveling_files = current_->leveling_files_[0].size();
  int tiering_files = current_->NumTieringFilesInLevel(0);
  int total_files = leveling_files + tiering_files;

  Log(options_->info_log, "Level 0: leveling_files = %d, tiering_files = %d, total_files = %d", leveling_files, tiering_files, total_files);

  if (current_->compaction_score_ >= 1.00 || current_->tieirng_compaction_score_ >= 1.00) {
    int file_diff = std::abs(leveling_files - tiering_files);
    int adjust_amount = file_diff / 2;

    if (file_diff > 1) {
    } else if (file_diff == 1) {
      adjust_amount = 1;
    } else if (file_diff < 1) {
      return true;
    }
    
    bool adjusted = false;
    if (leveling_files > tiering_files) {
      // 如果 leveling 文件更多，减少 leveling 增加 tiering
      if (config::kL0_StopWritesTrigger + adjust_amount >= 2 && config::kTiering_and_leveling_Multiplier - adjust_amount >= 2) {   
        config::kL0_StopWritesTrigger += adjust_amount;
        config::kTiering_and_leveling_Multiplier -= adjust_amount;
        Log(options_->info_log, "Level 0: Adjusting ratios - leveling_files > tiering_files, new kL0_StopWritesTrigger = %d, new kTiering_and_leveling_Multiplier = %d, adjust_amount = %d",
            config::kL0_StopWritesTrigger, config::kTiering_and_leveling_Multiplier, adjust_amount);
        adjusted = true;
      }
    } else {
      // 调整 kL0_StopWritesTrigger 和 kTiering_and_leveling_Multiplier
      if (config::kL0_StopWritesTrigger - adjust_amount >= 2 && config::kTiering_and_leveling_Multiplier + adjust_amount >= 2) {
        config::kL0_StopWritesTrigger -= adjust_amount;
        config::kTiering_and_leveling_Multiplier += adjust_amount;
        Log(options_->info_log, "Level 0: Adjusting ratios - leveling_files <= tiering_files, new kL0_StopWritesTrigger = %d, new kTiering_and_leveling_Multiplier = %d, adjust_amount = %d",
            config::kL0_StopWritesTrigger, config::kTiering_and_leveling_Multiplier, adjust_amount);
        adjusted = true;
      }
    }

    if (adjusted) {
      // 更新 compaction_score
      Finalize(current_);
      Log(options_->info_log, "Level 0: Updated compaction scores - compaction_score_ = %f, tieirng_compaction_score_ = %f", current_->compaction_score_, current_->tieirng_compaction_score_);
      return false;
    }

    return true;
  }
  return false; // 表明不需要进行 compaction
}


bool VersionSet::DetermineCompaction(int& level) {
  
  if(level == -1){
    return false;
  }

  if(level == 0){
    // return DetermineLevel0Compaction(level);
    assert(current_->compaction_score_ >= 1.00 || current_->tieirng_compaction_score_ >= 1.00);
    return true;
  }

  if(current_->compaction_score_ >= 1.00 || current_->tieirng_compaction_score_ >= 1.00) {

    double leveling_space = CompactionConfig::adaptive_compaction_configs[current_->compaction_level_]->leveling_space;
    double tiering_space = CompactionConfig::adaptive_compaction_configs[current_->compaction_level_]->tiering_space;

    double leveling_used = TotalFileSize(current_->leveling_files_[current_->compaction_level_]);
    double tiering_used = TotalFileSize(current_->tiering_runs_[current_->compaction_level_]);

    double leveling_free = leveling_space - leveling_used;
    double tiering_free = tiering_space - tiering_used;

    uint64_t next_sstable_size = options_->max_file_size;

    Log(options_->info_log, "Level %d: leveling_space = %f, tiering_space = %f, leveling_used = %f, tiering_used = %f, leveling_free = %f, tiering_free = %f, next_sstable_size = %lu",
        level, leveling_space, tiering_space, leveling_used, tiering_used, leveling_free, tiering_free, next_sstable_size);

    double adjust_amount = std::abs(current_->compaction_score_ - current_->tieirng_compaction_score_) / 2;

    if (current_->compaction_score_ > current_->tieirng_compaction_score_) {
      // 如果 leveling 部分的分数较大，检查 tiering 部分的剩余空间
      if (tiering_free >= next_sstable_size) {
        // 调整 leveling 和 tiering 的比例
        CompactionConfig::adaptive_compaction_configs[level]->levling_ratio += adjust_amount;
        CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio -= adjust_amount;

        // 确保 ratio 不小于 0.1
        if (CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio < 0.1) {
          CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio = 0.1;
        }
        if (CompactionConfig::adaptive_compaction_configs[level]->levling_ratio > 0.9) {
          CompactionConfig::adaptive_compaction_configs[level]->levling_ratio = 0.9;
        }
        UpdateCompactionSpaces();
        Log(options_->info_log, "Level %d: Adjusted ratios - compaction_score > tieirng_compaction_score, new leveling_ratio = %f, new tiering_ratio = %f",
            level, CompactionConfig::adaptive_compaction_configs[level]->levling_ratio, CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio);
        return false;
      }
    } else {
      // 如果 tiering 部分的分数较大，检查 leveling 部分的剩余空间
      if (leveling_free >= next_sstable_size) {
        // 调整 leveling 和 tiering 的比例
        CompactionConfig::adaptive_compaction_configs[level]->levling_ratio -= adjust_amount;
        CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio += adjust_amount;

        // 确保 ratio 不小于 0.1
        if (CompactionConfig::adaptive_compaction_configs[level]->levling_ratio < 0.1) {
          CompactionConfig::adaptive_compaction_configs[level]->levling_ratio = 0.1;
        }
        if (CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio > 0.9) {
          CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio = 0.9;
        }
        UpdateCompactionSpaces(); 
        Log(options_->info_log, "Level %d: Adjusted ratios - compaction_score <= tieirng_compaction_score, new leveling_ratio = %f, new tiering_ratio = %f",
            level, CompactionConfig::adaptive_compaction_configs[level]->levling_ratio, CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio);
        return false;
      }
    }
    return true;
  }
  return false;
}

void VersionSet::CreateCompactionForPartitionLeveling(std::vector<Compaction*>& compactions) {

  for (const auto& entry : current_->partitions_to_merge_) {
    uint64_t partition_number = entry.first;
    int level = entry.second;

    Compaction* c = new Compaction(options_, level, partition_number);

    // Pick the first file that comes after compact_pointer_[level][partition_number]
    auto it = compact_pointer_[level].find(partition_number);
    if (it != compact_pointer_[level].end()) {
      const std::string& compact_pointer = it->second;
      for (size_t i = 0; i < current_->partitioning_leveling_files_[level][partition_number].size(); i++) {
        FileMetaData* f = current_->partitioning_leveling_files_[level][partition_number][i];
        if (icmp_.Compare(f->largest.Encode(), compact_pointer_[level][partition_number]) > 0) {
          c->inputs_[0].push_back(f);
          Log(options_->leveling_info_log, "DetermineCompaction: Adding leveling file with largest key=%s", 
              f->largest.DebugString().c_str());
          break;
        }
      }
    }

    if (c->inputs_[0].empty()) {
      // Wrap-around to the beginning of the key space
      c->inputs_[0].push_back(current_->partitioning_leveling_files_[level][partition_number][0]);
      Log(options_->leveling_info_log, "DetermineCompaction: Wrap-around, adding first leveling file with largest key=%s", 
        c->inputs_[0][0]->largest.DebugString().c_str());
    }

    c->compaction_type = 1;

    compactions.push_back(c);
  }

}

void VersionSet::RePickCompaction(Compaction* leveling_compaction, std::vector<uint64_t>& merge_and_deletes){
  leveling_compaction->set_partition_num(merge_and_deletes.back());
  leveling_compaction->inputs_[0].clear();

  Log(options_->leveling_info_log, "Merged into partition: %lu, deleted partitions: %s",
    merge_and_deletes.back(),[&]() {
    std::string result;
    for (size_t i = 0; i < merge_and_deletes.size() - 1; ++i) {
      result += std::to_string(merge_and_deletes[i]) + " ";
    }
    return result;
    }().c_str());
  
  
  // Iterate over the partitions to be deleted and collect their L0 files
  for (size_t i = 0; i < merge_and_deletes.size() - 1; ++i) {
    uint64_t partition_num = merge_and_deletes[i];
    for (int i=0; i<current_->partitioning_leveling_files_[0][partition_num].size();i++) {
      leveling_compaction->inputs_[0].push_back(current_->partitioning_leveling_files_[0][partition_num][i]);
      leveling_compaction->merged_partitions_.push_back(partition_num);
    }
    leveling_compaction->small_merge_partitions += std::to_string(merge_and_deletes[i]) + " ";
    Log(options_->leveling_info_log, "Added %ld(double verfied: %lu) L0 files from partition: %lu to compaction inputs",
      leveling_compaction->merged_partitions_.size(), leveling_compaction->inputs_[0].size(), partition_num);
  }

  leveling_compaction->set_merge_compaction();
  return ;
}


void VersionSet::PickCompaction(std::vector<Compaction*>& leveling_compactions, TieringCompaction** tiering_compaction) {

  bool leveling_size_compaction = (current_->compaction_score_ >= 1.00);
  bool tiering_size_compaction  = (current_->tieirng_compaction_score_ >= 1.00);
  bool leveling_seek_compaction = (current_->file_to_compact_in_leveling != nullptr);
  bool tiering_seek_compaction  = (current_->file_to_compact_in_tiering != nullptr);

  Log(options_->info_log, "PickCompaction: Address of tiering_compaction pointer: %p", (void*)tiering_compaction);
  Log(options_->info_log, "PickCompaction: Address stored in tiering_compaction: %p", (void*)*tiering_compaction);


  Log(options_->info_log, "PickCompaction: leveling_size_compaction=%d, tiering_size_compaction=%d, leveling_seek_compaction=%d, tiering_seek_compaction=%d",
    leveling_size_compaction, tiering_size_compaction, leveling_seek_compaction, tiering_seek_compaction);

  Log(options_->leveling_info_log, "PickCompaction: leveling_size_compaction=%d, tiering_size_compaction=%d, leveling_seek_compaction=%d, tiering_seek_compaction=%d",
    leveling_size_compaction, tiering_size_compaction, leveling_seek_compaction, tiering_seek_compaction);

  if(leveling_size_compaction || tiering_size_compaction){
    if (leveling_size_compaction ) {
      CreateCompactionForPartitionLeveling(leveling_compactions);
    } 

    if(tiering_size_compaction){
      int tier_level = current_->tiering_compaction_level_;
      (*tiering_compaction) = new TieringCompaction(options_, tier_level);

      // Log new pointer address
      Log(options_->info_log, "PickCompaction: New address stored in tiering_compaction: %p", (void*)*tiering_compaction);


      int oldest_compaction_pointer = -1;
      int selected_run_in_input_level1 = -1;

      for (const auto& entry : tiering_compact_pointer_[tier_level]) {
        if (entry.second > oldest_compaction_pointer) {
          oldest_compaction_pointer = entry.second;
          selected_run_in_input_level1 = entry.first;
        }
      }

      (*tiering_compaction)->tiering_inputs_[0].push_back(current_->tiering_runs_[tier_level][selected_run_in_input_level1][oldest_compaction_pointer]);
      (*tiering_compaction)->selected_run_in_input_level = selected_run_in_input_level1;
      (*tiering_compaction)->set_tiering();
      FileMetaData* selected_file = current_->tiering_runs_[tier_level][selected_run_in_input_level1][oldest_compaction_pointer];

      // Consolidated log output
      Log(options_->info_log, "Tiering compaction: level=%d, run=%d, file_number=%llu, file_size=%llu, selected run in input level: %d, oldest compaction pointer: %d",
        tier_level, selected_run_in_input_level1, 
        (unsigned long long)selected_file->number, 
        (unsigned long long)selected_file->file_size,
        selected_run_in_input_level1, oldest_compaction_pointer);

      (*tiering_compaction)->compaction_type = 1;
    }else{
      *tiering_compaction = nullptr;
    }
  }else if(tiering_seek_compaction || leveling_seek_compaction){
    // If the tiering part also needs to be compressed, merge the tiering files.
    // if (leveling_seek_compaction ) {
    //   level = current_->file_to_compact_level_in_leveling;
    //   tiering_compaction = new Compaction(options_, level);
    //   c->inputs_[0].push_back(current_->file_to_compact_in_leveling);
    //   Log(options_->info_log, "PickCompaction: seek_compaction - level=%d", level);
    //   // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
    //   c->compaction_type = 2;
    //   // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
    // }

    if (tiering_seek_compaction){
      int level = current_->file_to_compact_level_in_leveling;
      (*tiering_compaction) = new TieringCompaction(options_, level);
      (*tiering_compaction)->inputs_[0].push_back(current_->file_to_compact_in_leveling);
      Log(options_->info_log, "PickCompaction: seek_compaction - level=%d", level);
      (*tiering_compaction)->compaction_type = 2;
    }
  }else {
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~
    Log(options_->info_log, "PickCompaction: no compaction needed"); 
    return ;
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
  }


  if(!leveling_compactions.empty()){
    for(int i=0; i<leveling_compactions.size(); i++){
      InternalKey smallest, largest;

      leveling_compactions[i]->input_version_ = current_;
      leveling_compactions[i]->input_version_->Ref();

      if(leveling_compactions[i]->level_ == 0 || leveling_compactions[i]->level_ == 1){
        GetRange(leveling_compactions[i]->inputs_[0], &smallest, &largest);
        current_->GetOverlappingInputs(leveling_compactions[i]->level_, &smallest, &largest, &leveling_compactions[i]->inputs_[0], leveling_compactions[i]->partition_num());
        assert(!leveling_compactions[i]->inputs_[0].empty());
      }

      Log(options_->leveling_info_log, "PickCompaction: level %d compaction - smallest=%s, largest=%s",leveling_compactions[i]->level_, smallest.DebugString().c_str(), largest.DebugString().c_str());
      if(leveling_compactions[i]->level_ > 0){
        SetupOtherInputs(leveling_compactions[i]);
      }
    }
  }
  
  if((*tiering_compaction) != nullptr){
    
    (*tiering_compaction)->input_version_ = current_;
    (*tiering_compaction)->input_version_->Ref();

    if((*tiering_compaction)->level_ == 0){
      InternalKey smallest, largest;
      GetRange((*tiering_compaction)->tiering_inputs_[0], &smallest, &largest);
      current_->GetOverlappingInputsWithTier(0, &smallest, &largest, &(*tiering_compaction)->tiering_inputs_[0], (*tiering_compaction)->selected_run_in_input_level);
      assert(!(*tiering_compaction)->tiering_inputs_[0].empty());
      Log(options_->info_log, "PickCompaction: Tiering level 0 compaction - smallest=%s, largest=%s", smallest.DebugString().c_str(), largest.DebugString().c_str());
    }
    SetupOtherInputsWithTier((*tiering_compaction));
  }

  return ;
}


// Finds the largest key in a vector of files. Returns true if files is not
// empty.
bool FindLargestKey(const InternalKeyComparator& icmp,
                    const std::vector<FileMetaData*>& files,
                    InternalKey* largest_key) {
  if (files.empty()) {
    return false;
  }
  *largest_key = files[0]->largest;
  for (size_t i = 1; i < files.size(); ++i) {
    FileMetaData* f = files[i];
    if (icmp.Compare(f->largest, *largest_key) > 0) {
      *largest_key = f->largest;
    }
  }
  return true;
}

// Finds minimum file b2=(l2, u2) in level file for which l2 > u1 and
// user_key(l2) = user_key(u1)
FileMetaData* FindSmallestBoundaryFile(
    const InternalKeyComparator& icmp,
    const std::vector<FileMetaData*>& level_files,
    const InternalKey& largest_key) {
  const Comparator* user_cmp = icmp.user_comparator();
  FileMetaData* smallest_boundary_file = nullptr;
  for (size_t i = 0; i < level_files.size(); ++i) {
    FileMetaData* f = level_files[i];
    if (icmp.Compare(f->smallest, largest_key) > 0 &&
        user_cmp->Compare(f->smallest.user_key(), largest_key.user_key()) ==
            0) {
      if (smallest_boundary_file == nullptr ||
          icmp.Compare(f->smallest, smallest_boundary_file->smallest) < 0) {
        smallest_boundary_file = f;
      }
    }
  }
  return smallest_boundary_file;
}

// Extracts the largest file b1 from |compaction_files| and then searches for a
// b2 in |level_files| for which user_key(u1) = user_key(l2). If it finds such a
// file b2 (known as a boundary file) it adds it to |compaction_files| and then
// searches again using this new upper bound.
//
// If there are two blocks, b1=(l1, u1) and b2=(l2, u2) and
// user_key(u1) = user_key(l2), and if we compact b1 but not b2 then a
// subsequent get operation will yield an incorrect result because it will
// return the record from b2 in level i rather than from b1 because it searches
// level by level for records matching the supplied user key.
//
// parameters:
//   in     level_files:      List of files to search for boundary files.
//   in/out compaction_files: List of files to extend by adding boundary files.
void AddBoundaryInputs(const InternalKeyComparator& icmp,
                       const std::vector<FileMetaData*>& level_files,
                       std::vector<FileMetaData*>* compaction_files) {
  InternalKey largest_key;

  // Quick return if compaction_files is empty.
  if (!FindLargestKey(icmp, *compaction_files, &largest_key)) {
    return;
  }

  bool continue_searching = true;
  while (continue_searching) {
    FileMetaData* smallest_boundary_file =
        FindSmallestBoundaryFile(icmp, level_files, largest_key);

    // If a boundary file was found advance largest_key, otherwise we're done.
    if (smallest_boundary_file != NULL) {
      compaction_files->push_back(smallest_boundary_file);
      largest_key = smallest_boundary_file->largest;
    } else {
      continue_searching = false;
    }
  }
}

void VersionSet::SetupOtherInputs(Compaction* c) {
  const int level = c->level();

  InternalKey smallest, largest;
  const uint64_t partition_number = c->partition_num();

  // find boundary files and add to compaction then get new range
  // Possibly extends inputs_[0] 
  AddBoundaryInputs(icmp_, current_->partitioning_leveling_files_[level][partition_number], &c->inputs_[0]);
  GetRange(c->inputs_[0], &smallest, &largest);

  // also find boundary files for the next level and add to inputs_[1]
  current_->GetOverlappingInputs(level + 1, &smallest, &largest, &c->inputs_[1],partition_number);
  AddBoundaryInputs(icmp_, current_->partitioning_leveling_files_[level + 1][partition_number], &c->inputs_[1]);

  // Get entire range covered by compaction
  // insert all files from inputs_[0] and inputs_[1] then utilize GetRange() to get lagger range 
  InternalKey all_start, all_limit;
  GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);

  // See if we can grow the number of inputs in "level" without
  // changing the number of "level+1" files we pick up.
  if (!c->inputs_[1].empty()) {

    std::vector<FileMetaData*> expanded0;
    current_->GetOverlappingInputs(level, &all_start, &all_limit, &expanded0,partition_number);
    AddBoundaryInputs(icmp_, current_->partitioning_leveling_files_[level][partition_number], &expanded0);
    const int64_t inputs0_size = TotalFileSize(c->inputs_[0]);
    const int64_t inputs1_size = TotalFileSize(c->inputs_[1]);
    const int64_t expanded0_size = TotalFileSize(expanded0);

    if (expanded0.size() > c->inputs_[0].size() &&
        inputs1_size + expanded0_size <
            ExpandedCompactionByteSizeLimit(options_)) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<FileMetaData*> expanded1;
      current_->GetOverlappingInputs(level + 1, &new_start, &new_limit,
                                     &expanded1,partition_number);
      AddBoundaryInputs(icmp_, current_->partitioning_leveling_files_[level + 1][partition_number], &expanded1);
      if (expanded1.size() == c->inputs_[1].size()) {
        Log(options_->leveling_info_log,
            "Expanding@%d %d+%d (%ld+%ld bytes) to %d+%d (%ld+%ld bytes)\n",
            level, int(c->inputs_[0].size()), int(c->inputs_[1].size()),
            long(inputs0_size), long(inputs1_size), int(expanded0.size()),
            int(expanded1.size()), long(expanded0_size), long(inputs1_size));
        smallest = new_start;
        largest = new_limit;
        c->inputs_[0] = expanded0;
        c->inputs_[1] = expanded1;
        GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);
      }
    }
  }


  // Compute the set of grandparent files that overlap this compaction
  // (parent == level+1; grandparent == level+2)
  if (level + 2 < config::kNumLevels) {
    current_->GetOverlappingInputs(level + 2, &all_start, &all_limit,
                                   &c->grandparents_);
  }

  // Update the place where we will do the next compaction for this level.
  // We update this immediately instead of waiting for the VersionEdit
  // to be applied so that if the compaction fails, we will try a different
  // key range next time.
  compact_pointer_[level][partition_number] = largest.Encode().ToString();
  c->edit_.SetCompactPointer(level,partition_number, largest);

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  set_overlap_range(c->inputs_[0], c->inputs_[1]);
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

}


void VersionSet::SetupOtherInputsWithTier(TieringCompaction* c) {
  const int level = c->level();
  InternalKey smallest, largest;
  const int selected_run = c->get_selected_run_in_input_level();
  // find boundary files and add to compaction then get new range
  // Possibly extends tiering_inputs_[0] 
  // AddBoundaryInputs(icmp_, current_->leveling_files_[level], &c->tiering_inputs_[0]);
  GetRange(c->tiering_inputs_[0], &smallest, &largest);

  // also find boundary files for the next level and add to inputs_[1]
  current_->GetOverlappingInputsWithTier(level + 1, &smallest, &largest, &c->tiering_inputs_[1], c->selected_run_in_next_level);
  // AddBoundaryInputs(icmp_, current_->leveling_files_[level + 1], &c->inputs_[1]);

  // Get entire range covered by compaction
  // insert all files from inputs_[0] and inputs_[1] then utilize GetRange() to get lagger range 
  InternalKey all_start, all_limit;
  GetRange2(c->tiering_inputs_[0], c->tiering_inputs_[1], &all_start, &all_limit);

  // See if we can grow the number of inputs in "level" without
  // changing the number of "level+1" files we pick up.
  if (!c->inputs_[1].empty()) {
    std::vector<FileMetaData*> expanded0;
    current_->GetOverlappingInputs(level, &all_start, &all_limit, &expanded0);
    AddBoundaryInputs(icmp_, current_->leveling_files_[level], &expanded0);
    const int64_t inputs0_size = TotalFileSize(c->inputs_[0]);
    const int64_t inputs1_size = TotalFileSize(c->inputs_[1]);
    const int64_t expanded0_size = TotalFileSize(expanded0);

    if (expanded0.size() > c->inputs_[0].size() &&
        inputs1_size + expanded0_size <
            ExpandedCompactionByteSizeLimit(options_)) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<FileMetaData*> expanded1;
      current_->GetOverlappingInputs(level + 1, &new_start, &new_limit,
                                     &expanded1);
      AddBoundaryInputs(icmp_, current_->leveling_files_[level + 1], &expanded1);
      if (expanded1.size() == c->inputs_[1].size()) {
        Log(options_->info_log,
            "Expanding@%d %d+%d (%ld+%ld bytes) to %d+%d (%ld+%ld bytes)\n",
            level, int(c->inputs_[0].size()), int(c->inputs_[1].size()),
            long(inputs0_size), long(inputs1_size), int(expanded0.size()),
            int(expanded1.size()), long(expanded0_size), long(inputs1_size));
        smallest = new_start;
        largest = new_limit;
        c->inputs_[0] = expanded0;
        c->inputs_[1] = expanded1;
        GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);
      }
    }
  }


  // Compute the set of grandparent files that overlap this compaction
  // (parent == level+1; grandparent == level+2)
  if (level + 2 < config::kNumLevels) {
    current_->GetOverlappingInputs(level + 2, &all_start, &all_limit,
                                   &c->grandparents_);
  }

  // Update the place where we will do the next compaction for this level.
  // We update this immediately instead of waiting for the VersionEdit
  // to be applied so that if the compaction fails, we will try a different
  // key range next time.
  // tiering_compact_pointer_[level][selected_run] = largest.Encode().ToString();
  c->edit_.SetCompactPointer(level, selected_run, largest);

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  set_overlap_range(c->inputs_[0], c->inputs_[1]);
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

}

Compaction* VersionSet::CompactRange(int level, const InternalKey* begin,
                                     const InternalKey* end) {
  std::vector<FileMetaData*> inputs;
  current_->GetOverlappingInputs(level, begin, end, &inputs);
  if (inputs.empty()) {
    return nullptr;
  }

  // Avoid compacting too much in one shot in case the range is large.
  // But we cannot do this for level-0 since level-0 files can overlap
  // and we must not pick one file and drop another older file if the
  // two files overlap.
  if (level > 0) {
    const uint64_t limit = MaxFileSizeForLevel(options_, level);
    uint64_t total = 0;
    for (size_t i = 0; i < inputs.size(); i++) {
      uint64_t s = inputs[i]->file_size;
      total += s;
      if (total >= limit) {
        inputs.resize(i + 1);
        break;
      }
    }
  }

  Compaction* c = new Compaction(options_, 0,level);
  c->input_version_ = current_;
  c->input_version_->Ref();
  c->inputs_[0] = inputs;
  SetupOtherInputs(c);
  return c;
}

Compaction::Compaction(const Options* options, int level, uint64_t partition)
    : level_(level),
      partition_num_(partition),
      max_output_file_size_(MaxFileSizeForLevel(options, level)),
      min_output_file_size_(MinFileSizeForLevel(options, level)),
      input_version_(nullptr),
      grandparent_index_(0),
      seen_key_(false),
      overlapped_bytes_(0),
      compaction_type(0),
      is_small_merge_compaction_(false) {
  for (int i = 0; i < config::kNumLevels; i++) {
    level_ptrs_[i] = 0;
  }
}

Compaction::~Compaction() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
  }
}

bool Compaction::IsTrivialMove() const {
  const VersionSet* vset = input_version_->vset_;
  // Avoid a move if there is lots of overlapping grandparent data.
  // Otherwise, the move could create a parent file that will require
  // a very expensive merge later on.
  return (num_input_files(0) == 1 && num_input_files(1) == 0 &&
          TotalFileSize(grandparents_) <=
              MaxGrandParentOverlapBytes(vset->options_));
}

void Compaction::AddInputDeletions(VersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < inputs_[which].size(); i++) {
      edit->RemoveFile(level_ + which, inputs_[which][i]->number);
    }
  }
}

void Compaction::AddPartitionInputDeletions(uint64_t partition_num, VersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < inputs_[which].size(); i++) {
      edit->RemovePartitionFile( level_ + which,partition_num, inputs_[which][i]->number);
    }
  }
}

void Compaction::AddL0MergePartitionInputDeletions(VersionEdit* edit) {
  assert(is_small_merge_compaction_);
  assert(inputs_[1].size() == 0);
  
  for (size_t i = 0; i < inputs_[0].size(); i++) {
    edit->RemovePartitionFile(level_ ,merged_partitions_[i], inputs_[0][i]->number);
  }
}

bool Compaction::IsBaseLevelForKey(const Slice& user_key) {
  // Maybe use binary search to find right entry instead of linear search?
  const Comparator* user_cmp = input_version_->vset_->icmp_.user_comparator();
  for (int lvl = level_ + 2; lvl < config::kNumLevels; lvl++) {
    const std::vector<FileMetaData*>& files = input_version_->leveling_files_[lvl];
    while (level_ptrs_[lvl] < files.size()) {
      FileMetaData* f = files[level_ptrs_[lvl]];
      if (user_cmp->Compare(user_key, f->largest.user_key()) <= 0) {
        // We've advanced far enough
        if (user_cmp->Compare(user_key, f->smallest.user_key()) >= 0) {
          // Key falls in this file's range, so definitely not base level
          return false;
        }
        break;
      }
      level_ptrs_[lvl]++;
    }
  }
  return true;
}

bool Compaction::ShouldStopBefore(const Slice& internal_key) {
  const VersionSet* vset = input_version_->vset_;
  // Scan to find earliest grandparent file that contains key.
  const InternalKeyComparator* icmp = &vset->icmp_;
  while (grandparent_index_ < grandparents_.size() &&
         icmp->Compare(internal_key,
                       grandparents_[grandparent_index_]->largest.Encode()) >
             0) {
    if (seen_key_) {
      overlapped_bytes_ += grandparents_[grandparent_index_]->file_size;
    }
    grandparent_index_++;
  }
  seen_key_ = true;

  if (overlapped_bytes_ > MaxGrandParentOverlapBytes(vset->options_)) {
    // Too much overlap for current output; start new output
    overlapped_bytes_ = 0;
    return true;
  } else {
    return false;
  }
}

void Compaction::ReleaseInputs() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
    input_version_ = nullptr;
  }
}

//  ~~~~~~~~~~~~~~~~~~~~~~ WZZ's comments for his adding source codes
//  ~~~~~~~~~~~~~~~~~~~~~~
int Compaction::get_compaction_type() { return compaction_type; }





TieringCompaction::TieringCompaction(const Options* options, int level)
    : level_(level),
      max_output_file_size_(MaxFileSizeForLevel(options, level)),
      max_tiering_file_size_(MaxTieringFileSizeForLevel(options, level)),
      input_version_(nullptr),
      grandparent_index_(0),
      seen_key_(false),
      overlapped_bytes_(0),
      compaction_type(0),
      is_tiering(false),
      selected_run_in_next_level(-1) {
  for (int i = 0; i < config::kNumLevels; i++) {
    level_ptrs_[i] = 0;
  }
}

TieringCompaction::~TieringCompaction() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
  }
}

bool TieringCompaction::IsTrivialMoveWithTier() const {
  const VersionSet* vset = input_version_->vset_;
  // Avoid a move if there is lots of overlapping grandparent data.
  // Otherwise, the move could create a parent file that will require
  // a very expensive merge later on.
  return (num_input_tier_files(0) == 1 && num_input_tier_files(1) == 0);
}

void TieringCompaction::AddInputDeletions(VersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < inputs_[which].size(); i++) {
      edit->RemoveFile(level_ + which, inputs_[which][i]->number);
    }
  }
}

void TieringCompaction::AddTieringInputDeletions(VersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < tiering_inputs_[which].size(); i++) {
      int run = (which == 0 ? selected_run_in_input_level : selected_run_in_next_level);
      edit->RemovetieringFile(level_ + which, run, tiering_inputs_[which][i]->number);
    }
  }
}

bool TieringCompaction::IsBaseLevelForKey(const Slice& user_key) {
  // Maybe use binary search to find right entry instead of linear search?
  const Comparator* user_cmp = input_version_->vset_->icmp_.user_comparator();
  for (int lvl = level_ + 2; lvl < config::kNumLevels; lvl++) {
    const std::vector<FileMetaData*>& files = input_version_->leveling_files_[lvl];
    while (level_ptrs_[lvl] < files.size()) {
      FileMetaData* f = files[level_ptrs_[lvl]];
      if (user_cmp->Compare(user_key, f->largest.user_key()) <= 0) {
        // We've advanced far enough
        if (user_cmp->Compare(user_key, f->smallest.user_key()) >= 0) {
          // Key falls in this file's range, so definitely not base level
          return false;
        }
        break;
      }
      level_ptrs_[lvl]++;
    }
  }
  return true;
}

bool TieringCompaction::ShouldStopBefore(const Slice& internal_key) {
  const VersionSet* vset = input_version_->vset_;
  // Scan to find earliest grandparent file that contains key.
  const InternalKeyComparator* icmp = &vset->icmp_;
  while (grandparent_index_ < grandparents_.size() &&
         icmp->Compare(internal_key,
                       grandparents_[grandparent_index_]->largest.Encode()) >
             0) {
    if (seen_key_) {
      overlapped_bytes_ += grandparents_[grandparent_index_]->file_size;
    }
    grandparent_index_++;
  }
  seen_key_ = true;

  if (overlapped_bytes_ > MaxGrandParentOverlapBytes(vset->options_)) {
    // Too much overlap for current output; start new output
    overlapped_bytes_ = 0;
    return true;
  } else {
    return false;
  }
}

void TieringCompaction::ReleaseInputs() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
    input_version_ = nullptr;
  }
}

//  ~~~~~~~~~~~~~~~~~~~~~~ WZZ's comments for his adding source codes
//  ~~~~~~~~~~~~~~~~~~~~~~
int TieringCompaction::get_compaction_type() { return compaction_type; }




}  // namespace leveldb
