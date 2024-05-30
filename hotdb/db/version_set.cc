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

static size_t TargetFileSize(const Options* options) {
  return options->max_file_size;
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

void Version::ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                                 bool (*func)(void*, int, FileMetaData*)) {

  int64_t start_time = vset_->env_->NowMicros(); // 开始查找metadata的时间                                  
  const Comparator* ucmp = vset_->icmp_.user_comparator();

  // Search level-0 in order from newest to oldest.
  std::vector<FileMetaData*> tmp;
  tmp.reserve(leveling_files_[0].size());
  for (uint32_t i = 0; i < leveling_files_[0].size(); i++) {
    FileMetaData* f = leveling_files_[0][i];
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

Status Version::Get(const ReadOptions& options, const LookupKey& k,
                    std::string* value, GetStats* stats) {
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

  ForEachOverlapping(state.saver.user_key, state.ikey, &state, &State::Match);

  return state.found ? state.s : Status::NotFound(Slice());
}



// ==== Start of modified code ====
int Version::NumFiles(int level) const {
  return leveling_files_[level].size()+NumTieringFilesInLevel(level);
}

int Version::NumLevelingFiles(int level) const {
  return leveling_files_[level].size();
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
        Log(vset_->options_->info_log, "Added file %llu to tier inputs for level %d", static_cast<unsigned long long>(f->number), level);

        // Level-0 files may overlap each other.  So check if the newly
        // added file has expanded the range.  If so, restart search.
        if (begin != nullptr && user_cmp->Compare(file_start, user_begin) < 0) {
          Log(vset_->options_->info_log, "File %llu expanded range on the left. Restarting search.", static_cast<unsigned long long>(f->number));
          user_begin = file_start;
          tier_inputs->clear();
          i = 0;
        } else if (end != nullptr && user_cmp->Compare(file_limit, user_end) > 0) {
          Log(vset_->options_->info_log, "File %llu expanded range on the right. Restarting search.", static_cast<unsigned long long>(f->number));
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
      Log(vset_->options_->info_log, "Run %d has %d overlapping files for level %d", run, num_overlappping_files_in_this_run, level);
      if(num_overlappping_files_in_this_run > num_overlapping_files){
        num_overlapping_files = num_overlappping_files_in_this_run;
        selected_run = run;
        Log(vset_->options_->info_log, "Selected run %d for level %d based on number of overlapping files", selected_run, level);
      }
    }

    Log(vset_->options_->info_log, "Selected run %d for level %d based on number of overlapping files", selected_run, level);

    for (size_t i = 0; i < tiering_runs_[level][selected_run].size();) {
      // iterate all files in the specific level
      FileMetaData* f = tiering_runs_[level][selected_run][i++];
      const Slice file_start = f->smallest.user_key();
      const Slice file_limit = f->largest.user_key();

      if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
      } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
      } else { 
        tier_inputs->push_back(f);
        Log(vset_->options_->info_log, "Added file %llu to tier inputs for level %d", static_cast<unsigned long long>(f->number), level);
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

  typedef std::set<FileMetaData*, BySmallestKey> Tiering_files_in_run;
  struct TieringLevelState {
    std::map<int, Tiering_files_in_run*> added_files_every_runs;
    std::map<int,std::set<uint64_t>> deleted_tiering_files;
  };

  VersionSet* vset_;
  Version* base_;
  LevelState levels_[config::kNumLevels];
  TieringLevelState tiering_levels_[config::kNumLevels];

 public:
  // Initialize a builder with the files from *base and other info from *vset
  Builder(VersionSet* vset, Version* base) : vset_(vset), base_(base) {
    base_->Ref();
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;

    for (int level = 0; level < config::kNumLevels; level++) {
      levels_[level].added_files = new FileSet(cmp);
    }

    for (int level = 0; level < config::kNumLevels; level++) {
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
      const FileSet* added = levels_[level].added_files;
      std::vector<FileMetaData*> to_unref;
      to_unref.reserve(added->size());
      for (FileSet::const_iterator it = added->begin(); it != added->end();
           ++it) {
        to_unref.push_back(*it);
      }
      delete added;
      for (uint32_t i = 0; i < to_unref.size(); i++) {
        FileMetaData* f = to_unref[i];
        f->refs--;
        if (f->refs <= 0) {
          delete f;
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

  // Apply all of the edits in *edit to the current state.
  void Apply(const VersionEdit* edit) {
    // Update compaction pointers
    for (size_t i = 0; i < edit->compact_pointers_.size(); i++) {
      const int level = edit->compact_pointers_[i].first;
      vset_->compact_pointer_[level] = edit->compact_pointers_[i].second.Encode().ToString();
    }

    // Delete files
    for (const auto& deleted_file_set_kvp : edit->deleted_files_) {
      const int level = deleted_file_set_kvp.first;
      const uint64_t number = deleted_file_set_kvp.second;
      levels_[level].deleted_files.insert(number);
      Log(vset_->options_->info_log, "Apply: Deleting file at level=%d, number=%lu", level, number);
    }

    // Add new files
    for (size_t i = 0; i < edit->new_files_.size(); i++) {
      const int level = edit->new_files_[i].first;
      FileMetaData* f = new FileMetaData(edit->new_files_[i].second);
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

      levels_[level].deleted_files.erase(f->number);
      levels_[level].added_files->insert(f);
      Log(vset_->options_->info_log, "Apply: Adding new file at level=%d, number=%lu, size=%lu", level, f->number, f->file_size);
    }
  }

  // Save the current state in *v.
  void SaveTo(Version* v) {
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;

    for (int level = 0; level < config::kNumLevels; level++) {

      // Merge the set of added files with the set of pre-existing files.
      // Drop any deleted files.  Store the result in *v.

      const std::vector<FileMetaData*>& base_files = base_->leveling_files_[level];
      std::vector<FileMetaData*>::const_iterator base_iter = base_files.begin();
      std::vector<FileMetaData*>::const_iterator base_end = base_files.end();

      const FileSet* added_files = levels_[level].added_files;
      v->leveling_files_[level].reserve(base_files.size() + added_files->size());

      for (const auto& added_file : *added_files) {
        // Add all smaller files listed in base_
        for (std::vector<FileMetaData*>::const_iterator bpos = std::upper_bound(base_iter, base_end, added_file, cmp); base_iter != bpos; ++base_iter) {
          MaybeAddFile(v, level, *base_iter);
        }

        MaybeAddFile(v, level, added_file);
      }

      // Add remaining base files
      for (; base_iter != base_end; ++base_iter) {
        MaybeAddFile(v, level, *base_iter);
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

  void MaybeAddFile(Version* v, int level, FileMetaData* f) {
    if (levels_[level].deleted_files.count(f->number) > 0) {
      // File is deleted: do nothing
    } else {
      std::vector<FileMetaData*>* files = &v->leveling_files_[level];
      if (level > 0 && !files->empty()) {
        // Must not overlap
        assert(vset_->icmp_.Compare((*files)[files->size() - 1]->largest, f->smallest) < 0);
      }
      f->refs++;
      files->push_back(f);
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
            MaybeAddFile(v, level,0, *base_iter);
            index++;
          }
          if(vset_->tiering_compact_pointer_[level][run_entry.first] == -1 || added_file->number > vset_->tiering_compact_pointer_[level][run_entry.first]){
            vset_->tiering_compact_pointer_[level][run_entry.first] = index;
          }
          MaybeAddFile(v, level,0, added_file);
          index++;
        }
        // Add remaining base files
        for (; base_iter != base_end; ++base_iter) {
          MaybeAddFile(v, level,0, *base_iter);
        }
      }

      // Handle leveling files
      v->leveling_files_[level] = base_->leveling_files_[level];
      for(int i=0;i<v->leveling_files_[level].size();i++){
        v->leveling_files_[level][i]->refs++;
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
  int best_level = -1;
  double best_score = -1;
  double best_tier_score = -1;

  for (int level = 0; level < config::kNumLevels - 1; level++) {
    double score, tier_score;
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
      score = v->NumLevelingFiles(0) /
              static_cast<double>(config::kL0_CompactionTrigger);
      tier_score = v->NumTieringFiles(0) /
              static_cast<double>(config::kTiering_and_leveling_Multiplier);
    } else {
      // Compute the ratio of current size to size limit.
      const uint64_t level_bytes = TotalFileSize(v->leveling_files_[level]);  
      score=static_cast<double>(level_bytes) / (MaxBytesForLevel(options_, level)*CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio);
      const uint64_t tiering_bytes = TotalFileSize(v->tiering_runs_[level]);
      tier_score = static_cast<double>(tiering_bytes) / (MaxBytesForLevel(options_, level)*CompactionConfig::adaptive_compaction_configs[level]->tieirng_ratio);
    }

    if ( (score+tier_score) > best_score) {
      best_level = level;
      best_score = score+tier_score;
      best_tier_score = tier_score;
    }
  }

  v->compaction_level_ = best_level;
  v->compaction_score_ = best_score - best_tier_score;
  v->tieirng_compaction_score_ = best_tier_score;
}

Status VersionSet::WriteSnapshot(log::Writer* log) {
  // TODO: Break up into multiple records to reduce memory usage on recovery?

  // Save metadata
  VersionEdit edit;
  edit.SetComparatorName(icmp_.user_comparator()->Name());

  // Save compaction pointers
  for (int level = 0; level < config::kNumLevels; level++) {
    if (!compact_pointer_[level].empty()) {
      InternalKey key;
      key.DecodeFrom(compact_pointer_[level]);
      edit.SetCompactPointer(level, key);
    }
  }

  // Save files
  for (int level = 0; level < config::kNumLevels; level++) {
    const std::vector<FileMetaData*>& files = current_->leveling_files_[level];
    for (size_t i = 0; i < files.size(); i++) {
      const FileMetaData* f = files[i];
      edit.AddFile(level, f->number, f->file_size, f->smallest, f->largest);
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
  return NumLevel_leveling_Files(level)+NumLevel_tiering_Files(level);
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
int VersionSet::NumLevel_tiering_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->NumTieringFilesInLevel(level);
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
int VersionSet::NumLevel_leveling_Files(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->leveling_files_[level].size();
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
void VersionSet::AddLiveFiles(std::set<uint64_t>* live) {
  for (Version* v = dummy_versions_.next_; v != &dummy_versions_; v = v->next_) {
    for (int level = 0; level < config::kNumLevels; level++) {
      Log(options_->info_log, "Version %p", static_cast<void*>(v));
      const std::vector<FileMetaData*>& leveling_files = v->leveling_files_[level];
      Log(options_->info_log, "  Level %d (leveling): %zu files", level, leveling_files.size());
      for (size_t i = 0; i < leveling_files.size(); i++) {
        live->insert(leveling_files[i]->number);
        Log(options_->info_log, "    Leveling File #%llu, size: %llu",
        (unsigned long long)leveling_files[i]->number,
        (unsigned long long)leveling_files[i]->file_size);
      }

      // Process tiering files in each run
      for (const auto& run_pair : v->tiering_runs_[level]) {
        int run = run_pair.first;
        const std::vector<FileMetaData*>& tiering_files = run_pair.second;
        Log(options_->info_log, "  Level %d (tiering, run %d): %zu files", level, run, tiering_files.size());
        for (size_t i = 0; i < tiering_files.size(); i++) {
          live->insert(tiering_files[i]->number);
          Log(options_->info_log, "    Tiering File #%llu, size: %llu",
              (unsigned long long)tiering_files[i]->number,
              (unsigned long long)tiering_files[i]->file_size);
        }
      }
    }
  }
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
  const int space = (c->level() == 0 ? c->inputs_[0].size() + 1 : 2);
  Iterator** list = new Iterator*[space];
  int num = 0;
  for (int which = 0; which < 2; which++) {
    if (!c->inputs_[which].empty()) {
      if (c->level() + which == 0) {
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

Iterator* VersionSet::MakeTieringInputIterator(Compaction* c) {
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

    if(file_diff > 1){
    }else if(file_diff == 1){
      adjust_amount = 1;
    }else if(file_diff <1){
      Log(options_->info_log, "Level 0: file_diff < 1, triggering compaction");
      return true;
    }
      
    if (leveling_files > tiering_files) {
      // 如果 leveling 文件更多，减少 leveling 增加 tiering
      if (config::kL0_StopWritesTrigger + adjust_amount >= 2 && config::kTiering_and_leveling_Multiplier - adjust_amount >= 2) {   
        config::kL0_StopWritesTrigger += adjust_amount;
        config::kTiering_and_leveling_Multiplier -= adjust_amount;
        Log(options_->info_log, "Level 0: Adjusting ratios - leveling_files > tiering_files, new kL0_StopWritesTrigger = %d, new kTiering_and_leveling_Multiplier = %d",
            config::kL0_StopWritesTrigger, config::kTiering_and_leveling_Multiplier);
        return false;
      }
    } else {
      // 调整 kL0_StopWritesTrigger 和 kTiering_and_leveling_Multiplier
      if (config::kL0_StopWritesTrigger - adjust_amount >= 2 && config::kTiering_and_leveling_Multiplier + adjust_amount >= 2) {
        config::kL0_StopWritesTrigger -= adjust_amount;
        config::kTiering_and_leveling_Multiplier += adjust_amount;

        Log(options_->info_log, "Level 0: Adjusting ratios - leveling_files <= tiering_files, new kL0_StopWritesTrigger = %d, new kTiering_and_leveling_Multiplier = %d",
            config::kL0_StopWritesTrigger, config::kTiering_and_leveling_Multiplier);

        return false;
      }
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
    return DetermineLevel0Compaction(level);
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

Compaction* VersionSet::PickCompaction() {
  Compaction* c;
  int level = -1;

  // Version* v = current_;  current_是当前可用的活动的version
  // We prefer compactions triggered by too much data in a level over
  // the compactions triggered by seeks.

  const bool size_compaction = DetermineCompaction(current_->compaction_level_);
  const bool seek_compaction = (current_->file_to_compact_in_leveling != nullptr);

  Log(options_->info_log, "PickCompaction: size_compaction=%d, seek_compaction=%d", size_compaction, seek_compaction);

  if (size_compaction) {
    level = current_->compaction_level_;
    assert(level >= 0); // 确保选定的层级有效
    assert(level + 1 < config::kNumLevels);// 确保有下一个层级存在，因为压缩操作可能会涉及到数据移动到下一层级
    
    // 创建一个新的压缩任务（Compaction），指定压缩操作的层级
    c = new Compaction(options_, level);
    Log(options_->info_log, "PickCompaction: size_compaction - level=%d, leveling_score=%.2f tiering_score=%.2f", 
        level, current_->compaction_score_, current_->tieirng_compaction_score_);

    if(current_->compaction_score_ >= 1.00){
      Log(options_->info_log, "DetermineCompaction: Compaction score >= 1.00 for level=%d, leveling_score=%.2f", 
        level, current_->compaction_score_);
      // Pick the first file that comes after compact_pointer_[level]
      for (size_t i = 0; i < current_->leveling_files_[level].size(); i++) {
        FileMetaData* f = current_->leveling_files_[level][i];
        if (compact_pointer_[level].empty() ||
            icmp_.Compare(f->largest.Encode(), compact_pointer_[level]) > 0) {
          c->inputs_[0].push_back(f);
          Log(options_->info_log, "DetermineCompaction: Adding leveling file with largest key=%s", 
            f->largest.Encode().ToString().c_str());
          break;
        }
      }
      if (c->inputs_[0].empty()) {
        // Wrap-around to the beginning of the key space
        c->inputs_[0].push_back(current_->leveling_files_[level][0]);
        Log(options_->info_log, "DetermineCompaction: Wrap-around, adding first leveling file with largest key=%s", 
          current_->leveling_files_[level][0]->largest.DebugString().c_str());
      }
    }
    
    // If the tiering part also needs to be compressed, merge the tiering files.
    if (current_->tieirng_compaction_score_ >= 1.00) {
      Log(options_->info_log, "DetermineCompaction: Tiering compaction score >= 1.00 for level=%d, tiering_score=%.2f", 
        level, current_->tieirng_compaction_score_);

      int oldest_compaction_pointer = -1;
      int selected_run_in_input_level1 = -1;
      for (int level = 0; level < config::kNumLevels - 1; level++) {
        for (const auto& entry : tiering_compact_pointer_[level]) {
          int compact_pointer = entry.second;
          if (compact_pointer > oldest_compaction_pointer) {
            oldest_compaction_pointer = compact_pointer;
            selected_run_in_input_level1 = entry.first;
          }
        }
      }
      c->tiering_inputs_[0].push_back(current_->tiering_runs_[level][selected_run_in_input_level1][oldest_compaction_pointer]);
      c->selected_run_in_input_level = selected_run_in_input_level1;
      c->set_tiering();
      FileMetaData* selected_file = current_->tiering_runs_[level][selected_run_in_input_level1][oldest_compaction_pointer];
      Log(options_->info_log, "Selected file for compaction: level=%d, run=%d, file_number=%llu, file_size=%llu", 
        level, selected_run_in_input_level1, 
        (unsigned long long)selected_file->number, 
        (unsigned long long)selected_file->file_size);
    }

    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
    c->compaction_type = 1;
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
  } else if (seek_compaction) {
    level = current_->file_to_compact_level_in_leveling;
    c = new Compaction(options_, level);
    c->inputs_[0].push_back(current_->file_to_compact_in_leveling);
    Log(options_->info_log, "PickCompaction: seek_compaction - level=%d", level);
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
    c->compaction_type = 2;
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 

  } else {
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~
    Log(options_->info_log, "PickCompaction: no compaction needed"); 
    return nullptr;
    c->compaction_type = 4;
    // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
    
  }

  // 将当前数据库的版本（Version）赋值给压缩任务（Compaction）对象的input_version_。
  // 这意味着当前的压缩任务将会基于这个版本的数据进行。
  c->input_version_ = current_;
  // 增加input_version_的引用计数，确保在压缩过程中该版本数据不会被删除或修改。
  c->input_version_->Ref();

  // Files in level 0 may overlap each other, so pick up all overlapping ones
  if (level == 0) {
    // 获取当前选定进行压缩的文件集的最小和最大键。
    InternalKey smallest, largest;
    if(c->get_is_tieirng()){
      GetRange(c->tiering_inputs_[0], &smallest, &largest);
    }else{
      GetRange(c->inputs_[0], &smallest, &largest);
    }
    Log(options_->info_log, "PickCompaction: level 0 compaction - smallest=%s, largest=%s", smallest.DebugString().c_str(), largest.DebugString().c_str());

    // Note that the next call will discard the file we placed in
    // c->inputs_[0] earlier and replace it with an overlapping set
    // which will include the picked file.
    if(c->get_is_tieirng()){
      current_->GetOverlappingInputsWithTier(0, &smallest, &largest, &c->tiering_inputs_[0], c->selected_run_in_next_level);
      assert(!c->tiering_inputs_[0].empty());
    }else{
      current_->GetOverlappingInputs(0, &smallest, &largest, &c->inputs_[0]);
      assert(!c->inputs_[0].empty());
    }
  }

  if(c->get_is_tieirng()){
    SetupOtherInputsWithTier(c);
  }else{
    SetupOtherInputs(c);
  }
  

  // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 
  if (c->IsTrivialMove()) {
    c->compaction_type = 3;
  }
  // ~~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~~ 

  return c;
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

  // find boundary files and add to compaction then get new range
  // Possibly extends inputs_[0] 
  AddBoundaryInputs(icmp_, current_->leveling_files_[level], &c->inputs_[0]);
  GetRange(c->inputs_[0], &smallest, &largest);

  // also find boundary files for the next level and add to inputs_[1]
  current_->GetOverlappingInputs(level + 1, &smallest, &largest, &c->inputs_[1]);
  AddBoundaryInputs(icmp_, current_->leveling_files_[level + 1], &c->inputs_[1]);

  // Get entire range covered by compaction
  // insert all files from inputs_[0] and inputs_[1] then utilize GetRange() to get lagger range 
  InternalKey all_start, all_limit;
  GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);

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
  compact_pointer_[level] = largest.Encode().ToString();
  c->edit_.SetCompactPointer(level, largest);

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  set_overlap_range(c->inputs_[0], c->inputs_[1]);
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

}


void VersionSet::SetupOtherInputsWithTier(Compaction* c) {
  const int level = c->level();
  InternalKey smallest, largest;

  // find boundary files and add to compaction then get new range
  // Possibly extends tiering_inputs_[0] 
  // AddBoundaryInputs(icmp_, current_->leveling_files_[level], &c->tiering_inputs_[0]);
  GetRange(c->tiering_inputs_[0], &smallest, &largest);

  // also find boundary files for the next level and add to inputs_[1]
  current_->GetOverlappingInputsWithTier(level + 1, &smallest, &largest, &c->inputs_[1], c->selected_run_in_next_level);
  AddBoundaryInputs(icmp_, current_->leveling_files_[level + 1], &c->inputs_[1]);

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
  compact_pointer_[level] = largest.Encode().ToString();
  c->edit_.SetCompactPointer(level, largest);

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

  Compaction* c = new Compaction(options_, level);
  c->input_version_ = current_;
  c->input_version_->Ref();
  c->inputs_[0] = inputs;
  SetupOtherInputs(c);
  return c;
}

Compaction::Compaction(const Options* options, int level)
    : level_(level),
      max_output_file_size_(MaxFileSizeForLevel(options, level)),
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

bool Compaction::IsTrivialMoveWithTier() const {
  const VersionSet* vset = input_version_->vset_;
  // Avoid a move if there is lots of overlapping grandparent data.
  // Otherwise, the move could create a parent file that will require
  // a very expensive merge later on.
  return (num_input_tier_files(0) == 1 && num_input_tier_files(1) == 0 &&
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

void Compaction::AddTieringInputDeletions(VersionEdit* edit, int run) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < tiering_inputs_[which].size(); i++) {
      edit->RemovetieringFile(level_ + which, run, tiering_inputs_[which][i]->number);
    }
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

}  // namespace leveldb
