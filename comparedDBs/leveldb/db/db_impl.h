// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_DB_IMPL_H_
#define STORAGE_LEVELDB_DB_DB_IMPL_H_

#include <atomic>
#include <deque>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <string>

#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "port/thread_annotations.h"



namespace leveldb {

class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;

class DBImpl : public DB {
 public:
  DBImpl(const Options& options, const std::string& dbname);

  DBImpl(const DBImpl&) = delete;
  DBImpl& operator=(const DBImpl&) = delete;

  ~DBImpl() override;

  // Implementations of the DB interface
  Status Put(const WriteOptions&, const Slice& key,
             const Slice& value) override;
  Status Delete(const WriteOptions&, const Slice& key) override;
  Status Write(const WriteOptions& options, WriteBatch* updates) override;
  Status Get(const ReadOptions& options, const Slice& key,
             std::string* value) override;
  Iterator* NewIterator(const ReadOptions&) override;
  const Snapshot* GetSnapshot() override;
  void ReleaseSnapshot(const Snapshot* snapshot) override;
  bool GetProperty(const Slice& property, std::string* value) override;
  void GetApproximateSizes(const Range* range, int n, uint64_t* sizes) override;
  void CompactRange(const Slice* begin, const Slice* end) override;

  // Extra methods (for testing) that are not in the public DB interface

  // Compact any files in the named level that overlap [*begin,*end]
  void TEST_CompactRange(int level, const Slice* begin, const Slice* end);


  // ██╗    ██╗███████╗███████╗
  // ██║    ██║╚══███╔╝╚══███╔╝
  // ██║ █╗ ██║  ███╔╝   ███╔╝ 
  // ██║███╗██║ ███╔╝   ███╔╝  
  // ╚███╔███╔╝███████╗███████╗
  //  ╚══╝╚══╝ ╚══════╝╚══════╝
  //  This function is an extension of the GetProperty from LevelDB, crafted by WZZ.
  //  It is designed to fetch comprehensive LSM (Log-Structured Merge-tree) related information,
  //  providing insights not just on the requested level, but across the entire LSM tree.
  //  This enhanced visibility is crucial for debugging and fine-tuning the store's performance and storage efficiency.
  bool GetProperty_with_whole_lsm(const Slice& property, std::string* value);

  // Force current memtable contents to be compacted.
  Status TEST_CompactMemTable();

  // Return an internal iterator over the current state of the database.
  // The keys of this iterator are internal keys (see format.h).
  // The returned iterator should be deleted when no longer needed.
  Iterator* TEST_NewInternalIterator();

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  int64_t TEST_MaxNextLevelOverlappingBytes();

  // Record a sample of bytes read at the specified internal key.
  // Samples are taken approximately once every config::kReadBytesPeriod
  // bytes.
  void RecordReadSample(Slice key);

 private:
  friend class DB;
  struct CompactionState;
  struct Writer;

  // Information for a manual compaction
  struct ManualCompaction {
    int level;
    bool done;
    const InternalKey* begin;  // null means beginning of key range
    const InternalKey* end;    // null means end of key range
    InternalKey tmp_storage;   // Used to keep track of compaction progress
  };

  // Per level compaction stats.  stats_[level] stores the stats for
  // compactions that produced data for the specified "level".
  struct CompactionStats {
    CompactionStats() : micros(0), bytes_read(0), bytes_written(0){}

    void Add(const CompactionStats& c) {
      this->micros += c.micros;
      this->bytes_read += c.bytes_read;
      this->bytes_written += c.bytes_written;
    }

    int64_t micros;
    int64_t bytes_read;
    int64_t bytes_written;
  };


    struct new_LeveldataStats {
    new_LeveldataStats()
    : micros(0), 
      bytes_read(0), 
      bytes_written(0), 
      bytes_read_hot(0), 
      bytes_written_hot(0), 
      number_of_compactions(0), 
      user_bytes_written(0), 
      moved_directly_from_last_level_bytes(0), 
      moved_from_this_level_bytes(0),
      number_size_compaction(0),
      number_size_compaction_initiator_files(0),
      number_size_compaction_participant_files(0),
      number_seek_compaction(0),
      number_seek_compaction_initiator_files(0),
      number_seek_compaction_participant_files(0),
      number_manual_compaction(0),
      number_TrivialMove(0),
      bytes_read_cold(0),
      bytes_written_cold(0) {}

    void Add(const new_LeveldataStats& c) {
      this->micros += c.micros;
      this->bytes_read += c.bytes_read;
      this->bytes_written += c.bytes_written;

      // Note: Assuming user_bytes_written should be accumulated as well.
      this->bytes_read_hot += c.bytes_read_hot;
      this->bytes_written_hot += c.bytes_written_hot;
      this->number_of_compactions += c.number_of_compactions;
      this->user_bytes_written += c.user_bytes_written;
      
    }

    int64_t micros;
    int64_t bytes_read;
    int64_t bytes_written;

    // Newly added fields
    int64_t bytes_read_hot;
    int64_t bytes_read_cold;
    int64_t bytes_written_hot;
    int64_t bytes_written_cold;
    int32_t number_of_compactions;
    int64_t user_bytes_written;
    int64_t moved_directly_from_last_level_bytes;
    int64_t moved_from_this_level_bytes;

    // Count of size compactions performed.
    // Number of files that initiated size compactions.
    // These are the files that directly triggered a size compaction due to exceeding certain thresholds.
    int32_t number_size_compaction;
    int32_t number_size_compaction_initiator_files;
    int32_t number_size_compaction_participant_files;

    // Count of seek compactions performed.
    int32_t number_seek_compaction;
    int32_t number_seek_compaction_initiator_files;
    int32_t number_seek_compaction_participant_files;

    int32_t number_manual_compaction;
    int32_t number_TrivialMove;
  };

  struct LevelHotColdStats {
    
    LevelHotColdStats() 
        : bytes_read_hot(0), 
          bytes_written_hot(0), 
          bytes_read_cold(0), 
          bytes_written_cold(0),
          total_count_hc(0) {}

    int64_t bytes_read_hot;
    int64_t bytes_written_hot;
    int64_t bytes_read_cold;
    int64_t bytes_written_cold;
    int64_t total_count_hc;
  };

  Iterator* NewInternalIterator(const ReadOptions&,
                                SequenceNumber* latest_snapshot,
                                uint32_t* seed);

  Status NewDB();

  // Recover the descriptor from persistent storage.  May do a significant
  // amount of work to recover recently logged updates.  Any changes to
  // be made to the descriptor are added to *edit.
  Status Recover(VersionEdit* edit, bool* save_manifest)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void MaybeIgnoreError(Status* s) const;

  // Delete any unneeded files and stale in-memory entries.
  void RemoveObsoleteFiles() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Compact the in-memory write buffer to disk.  Switches to a new
  // log-file/memtable and writes a new descriptor iff successful.
  // Errors are recorded in bg_error_.
  void CompactMemTable() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Status RecoverLogFile(uint64_t log_number, bool last_log, bool* save_manifest,
                        VersionEdit* edit, SequenceNumber* max_sequence)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Status WriteLevel0Table(MemTable* mem, VersionEdit* edit, Version* base)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Status MakeRoomForWrite(bool force /* compact even if there is room? */)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  WriteBatch* BuildBatchGroup(Writer** last_writer)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void RecordBackgroundError(const Status& s);

  void MaybeScheduleCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  static void BGWork(void* db);
  void BackgroundCall();
  void BackgroundCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void CleanupCompaction(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  Status DoCompactionWork(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Status OpenCompactionOutputFile(CompactionState* compact);
  Status FinishCompactionOutputFile(CompactionState* compact, Iterator* input);
  Status InstallCompactionResults(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const Comparator* user_comparator() const {
    return internal_comparator_.user_comparator();
  }

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  void loadKeysFromCSV(const std::string& filePath);

  bool isSpecialKey(const Slice& key) {
    return specialKeys.find(key) != specialKeys.end();
  }

  void testSpecialKeys();

  void load_keys_from_CSV(const std::string& filePath);

  void batch_load_keys_from_CSV(const std::string& filePath, const std::string& percentagesStr);

  void batch_load_keys_from_CSV2(const std::string& filePath, const std::string& percentagesStr);

  bool is_hot_key(uint64_t key) {
    return hot_keys.find(key) != hot_keys.end();
  }

  bool is_hot_key(int percentage, uint64_t key) {
    auto it = hot_keys_sets.find(percentage);
    if (it != hot_keys_sets.end()) {
        return it->second.find(key) != it->second.end();
    }
    return false; // 如果没有找到对应的percentage，返回false
  }


  void test_hot_keys();

  std::vector<int> GetLevelPercents();

  void initialize_level_hotcoldstats();

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

  // Constant after construction
  Env* const env_;
  const InternalKeyComparator internal_comparator_;
  const InternalFilterPolicy internal_filter_policy_;
  const Options options_;  // options_.comparator == &internal_comparator_
  const bool owns_info_log_;
  const bool owns_cache_;
  const std::string dbname_;

  // table_cache_ provides its own synchronization
  TableCache* const table_cache_;

  // Lock over the persistent DB state.  Non-null iff successfully acquired.
  FileLock* db_lock_;

  // State below is protected by mutex_
  port::Mutex mutex_;

  std::atomic<bool> shutting_down_;
  port::CondVar background_work_finished_signal_ GUARDED_BY(mutex_);
  MemTable* mem_;
  MemTable* imm_ GUARDED_BY(mutex_);  // Memtable being compacted
  std::atomic<bool> has_imm_;         // So bg thread can detect non-null imm_
  WritableFile* logfile_;
  uint64_t logfile_number_ GUARDED_BY(mutex_);
  log::Writer* log_;
  uint32_t seed_ GUARDED_BY(mutex_);  // For sampling.

  // Queue of writers.
  std::deque<Writer*> writers_ GUARDED_BY(mutex_);
  WriteBatch* tmp_batch_ GUARDED_BY(mutex_);

  SnapshotList snapshots_ GUARDED_BY(mutex_);

  // Set of table files to protect from deletion because they are
  // part of ongoing compactions.
  std::set<uint64_t> pending_outputs_ GUARDED_BY(mutex_);

  // Has a background compaction been scheduled or is running?
  bool background_compaction_scheduled_ GUARDED_BY(mutex_);

  ManualCompaction* manual_compaction_ GUARDED_BY(mutex_);

  VersionSet* const versions_ GUARDED_BY(mutex_);

  // Have we encountered a background error in paranoid mode?
  Status bg_error_ GUARDED_BY(mutex_);

  CompactionStats stats_[config::kNumLevels] GUARDED_BY(mutex_);

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  new_LeveldataStats level_stats_[config::kNumLevels] GUARDED_BY(mutex_);
  std::vector<std::map<unsigned, LevelHotColdStats>> level_hot_cold_stats GUARDED_BY(mutex_);
  std::pair<Slice, Slice> hot_range;

  struct SliceHash {
    size_t operator()(const leveldb::Slice& slice) const {
        return std::hash<std::string>()(std::string(slice.data(), slice.size()));
      }
  };

  // 定义一个自定义相等函数
  struct SliceEqual {
      bool operator()(const leveldb::Slice& lhs, const leveldb::Slice& rhs) const {
          return lhs.compare(rhs) == 0;
      }
  };

  const std::string hot_file_path;
  const std::string percentagesStr;
  std::unordered_set<leveldb::Slice, SliceHash, SliceEqual> specialKeys;
  std::unordered_set<uint64_t> hot_keys;
  std::map<int, std::unordered_set<uint64_t>> hot_keys_sets;
  std::map<int, std::unordered_map<uint64_t, int>> hot_keys_map;
  bool is_first;
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~


};

// Sanitize db options.  The caller should delete result.info_log if
// it is not equal to src.info_log.
Options SanitizeOptions(const std::string& db,
                        const InternalKeyComparator* icmp,
                        const InternalFilterPolicy* ipolicy,
                        const Options& src);

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_DB_IMPL_H_
