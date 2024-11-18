// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/db_impl.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <sstream>
#include <iomanip>  // 包含 setprecision
#include <set>
#include <queue>
#include <string>
#include <vector>

#include "db/builder.h"
#include "db/db_iter.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "db/table_cache.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/status.h"
#include "leveldb/table.h"
#include "leveldb/table_builder.h"
#include "port/port.h"
#include "table/block.h"
#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "global_stats.h"
#include "leveldb/json.hpp"

namespace leveldb {

const int kNumNonTableCacheFiles = 10;

// Information kept for every waiting writer
struct DBImpl::Writer {
  explicit Writer(port::Mutex* mu)
      : batch(nullptr), sync(false), done(false), cv(mu) {}

  Status status;
  WriteBatch* batch;
  bool sync;
  bool done;
  port::CondVar cv;
};

struct DBImpl::CompactionState {
  // Files produced by compaction
  struct Output {
    uint64_t number;
    uint64_t file_size;
    InternalKey smallest, largest;
  };

  Output* current_output() { return &outputs[outputs.size() - 1]; }

  Output* current_hot_output() { return &hot_outputs[hot_outputs.size() - 1]; }

  explicit CompactionState(Compaction* c)
      : compaction(c),
        smallest_snapshot(0),
        outfile(nullptr),
        builder(nullptr),
        hot_builder(nullptr),
        hot_outfile(nullptr),
        total_bytes(0) {}

  Compaction* const compaction;

  // Sequence numbers < smallest_snapshot are not significant since we
  // will never have to service a snapshot below smallest_snapshot.
  // Therefore if we have seen a sequence number S <= smallest_snapshot,
  // we can drop all entries for the same key with sequence numbers < S.
  SequenceNumber smallest_snapshot;

  std::vector<Output> outputs;
  std::vector<Output> hot_outputs;
  std::vector<uint64_t> L1_partitions_;

  // State kept for output being generated
  WritableFile* outfile;
  TableBuilder* builder;

  WritableFile* hot_outfile;
  TableBuilder* hot_builder;

  uint64_t total_bytes;
};

struct DBImpl::TieringCompactionState {
  // Files produced by compaction
  struct Output {
    uint64_t number;
    uint64_t file_size;
    InternalKey smallest, largest;
  };

  Output* current_output() { return &outputs[outputs.size() - 1]; }

  explicit TieringCompactionState(TieringCompaction* c)
      : tiercompaction(c),
        smallest_snapshot(0),
        outfile(nullptr),
        builder(nullptr),
        total_bytes(0) {}

  TieringCompaction* const tiercompaction;

  // Sequence numbers < smallest_snapshot are not significant since we
  // will never have to service a snapshot below smallest_snapshot.
  // Therefore if we have seen a sequence number S <= smallest_snapshot,
  // we can drop all entries for the same key with sequence numbers < S.
  SequenceNumber smallest_snapshot;

  std::vector<Output> outputs;

  // State kept for output being generated
  WritableFile* outfile;
  TableBuilder* builder;

  uint64_t total_bytes;
};

// Fix user-supplied options to be reasonable
template <class T, class V>
static void ClipToRange(T* ptr, V minvalue, V maxvalue) {
  if (static_cast<V>(*ptr) > maxvalue) *ptr = maxvalue;
  if (static_cast<V>(*ptr) < minvalue) *ptr = minvalue;
}
Options SanitizeOptions(const std::string& dbname,
                        const InternalKeyComparator* icmp,
                        const InternalFilterPolicy* ipolicy,
                        const Options& src) {
  Options result = src;
  result.comparator = icmp;
  result.filter_policy = (src.filter_policy != nullptr) ? ipolicy : nullptr;
  ClipToRange(&result.max_open_files, 64 + kNumNonTableCacheFiles, 50000);
  ClipToRange(&result.write_buffer_size, 64 << 10, 1 << 30);
  ClipToRange(&result.max_file_size, 1 << 20, 1 << 30);
  ClipToRange(&result.block_size, 1 << 10, 4 << 20);
  if (result.info_log == nullptr) {
    // Open a log file in the same directory as the db
    src.env->CreateDir(dbname);  // In case it does not exist
    src.env->RenameFile(InfoLogFileName(dbname), OldInfoLogFileName(dbname));
    Status s = src.env->NewLogger(InfoLogFileName(dbname), &result.info_log);
    if (!s.ok()) {
      // No place suitable for logging
      result.info_log = nullptr;
    }
  }

  // Initialize leveling_info_log
  if (result.leveling_info_log == nullptr) {
    // Open a log file specifically for leveling info log in the same directory as the db
    std::string leveling_log_filename = dbname + "/Leveling_LOG";
    Status s = src.env->NewLogger(leveling_log_filename, &result.leveling_info_log);
    if (!s.ok()) {
      // Handle error: fail to create leveling_info_log
      result.leveling_info_log = nullptr;
    }
  }

  if (result.leveling_compaction_info_log == nullptr) {
    // Open a log file specifically for leveling info log in the same directory as the db
    std::string leveling_log_filename = dbname + "/Leveling_CompactionLOG";
    Status s = src.env->NewLogger(leveling_log_filename, &result.leveling_compaction_info_log);
    if (!s.ok()) {
      // Handle error: fail to create leveling_compaction_info_log
      result.leveling_compaction_info_log = nullptr;
    }
  }

  if (result.block_cache == nullptr) {
    result.block_cache = NewLRUCache(8 << 20);
  }
  return result;
}

static int TableCacheSize(const Options& sanitized_options) {
  // Reserve ten files or so for other uses and give the rest to TableCache.
  return sanitized_options.max_open_files - kNumNonTableCacheFiles;
}

DBImpl::DBImpl(const Options& raw_options, const std::string& dbname)
    : env_(raw_options.env),
      internal_comparator_(raw_options.comparator),
      internal_filter_policy_(raw_options.filter_policy),
      options_(SanitizeOptions(dbname, &internal_comparator_,
                               &internal_filter_policy_, raw_options)),
      owns_info_log_(options_.info_log != raw_options.info_log),
      owns_cache_(options_.block_cache != raw_options.block_cache),
      is_first(false),
      dbname_(dbname),
      hot_file_path(raw_options.hot_file_path),
      percentagesStr(raw_options.percentages),
      table_cache_(new TableCache(dbname_, options_, TableCacheSize(options_))),
      db_lock_(nullptr),
      shutting_down_(false),
      background_work_finished_signal_(&mutex_),
      mem_(nullptr),
      imm_(nullptr),
      has_imm_(false),
      hot_key_threshold(options_.hot_frequency_identification), // default is 1
      hot_mem_(nullptr),
      hot_imm_(nullptr),
      has_hot_imm_(false),
      logfile_(nullptr),
      logfile_number_(0),
      log_(nullptr),
      seed_(0),
      HotRanges(new HotRangesContext()),
      ranges_upper_limit(100000),
      tmp_batch_(new WriteBatch),
      in_memory_batch(new WriteBatch),
      in_memory_hot_batch(new WriteBatch),
      background_compaction_scheduled_(false),
      in_memory_batch_kv_number(0),
      identifier_time(0),
      manual_compaction_(nullptr),
      versions_(new VersionSet(dbname_, &options_, table_cache_, &internal_comparator_)), 
      hot_key_identifier(new range_identifier(options_.init_range,10000)) {}

DBImpl::~DBImpl() {
  // Wait for background work to finish.
  mutex_.Lock();
  shutting_down_.store(true, std::memory_order_release);
  while (background_compaction_scheduled_) {
    background_work_finished_signal_.Wait();
  }
  


  SaveMemPartitionsToFile();
  SaveHotRangesToFile();

  mutex_.Unlock();

  if (db_lock_ != nullptr) {
    env_->UnlockFile(db_lock_);
  }

  delete versions_;
  if (mem_ != nullptr) mem_->Unref();
  if (imm_ != nullptr) imm_->Unref();
  delete tmp_batch_;
  delete in_memory_batch;
  delete log_;
  delete logfile_;
  delete table_cache_;

  if (owns_info_log_) {
    delete options_.info_log;
  }
  if (owns_cache_) {
    delete options_.block_cache;
  }

  // Call the cleanup function for compaction configs
  delete hot_key_identifier;
  CompactionConfig::CleanupCompactionConfigs();
}

Status DBImpl::NewDB() {
  VersionEdit new_db;
  new_db.SetComparatorName(user_comparator()->Name());
  new_db.SetLogNumber(0);
  new_db.SetNextFile(2);
  new_db.SetLastSequence(0);

  const std::string manifest = DescriptorFileName(dbname_, 1);
  WritableFile* file;
  Status s = env_->NewWritableFile(manifest, &file);
  if (!s.ok()) {
    return s;
  }
  {
    log::Writer log(file);
    std::string record;
    new_db.EncodeTo(&record);
    s = log.AddRecord(record);
    if (s.ok()) {
      s = file->Sync();
    }
    if (s.ok()) {
      s = file->Close();
    }
  }
  delete file;
  if (s.ok()) {
    // Make "CURRENT" file that points to the new manifest file.
    s = SetCurrentFile(env_, dbname_, 1);
  } else {
    env_->RemoveFile(manifest);
  }
  return s;
}

void DBImpl::MaybeIgnoreError(Status* s) const {
  if (s->ok() || options_.paranoid_checks) {
    // No change needed
  } else {
    Log(options_.info_log, "Ignoring error %s", s->ToString().c_str());
    *s = Status::OK();
  }
}

void DBImpl::RemoveObsoleteFiles() {
  mutex_.AssertHeld();

  if (!bg_error_.ok()) {
    // After a background error, we don't know whether a new version may
    // or may not have been committed, so we cannot safely garbage collect.
    return;
  }

  // Make a set of all of the live files
  std::set<uint64_t> live = pending_outputs_;
  versions_->AddLiveFiles(&live, true);

  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  FileType type;
  std::vector<std::string> files_to_delete;
  for (std::string& filename : filenames) {
    if (ParseFileName(filename, &number, &type)) {
      bool keep = true;
      switch (type) {
        case kLogFile:
          keep = ((number >= versions_->LogNumber()) ||
                  (number == versions_->PrevLogNumber()));
          // Log(options_.info_log, "Checking LogFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kDescriptorFile:
          // Keep my manifest file, and any newer incarnations'
          // (in case there is a race that allows other incarnations)
          keep = (number >= versions_->ManifestFileNumber());
          // Log(options_.info_log, "Checking DescriptorFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kTableFile:
          keep = (live.find(number) != live.end());
          // Log(options_.info_log, "Checking TableFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kTempFile:
          // Any temp files that are currently being written to must
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());
          // Log(options_.info_log, "Checking TempFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
          keep = true;
          // Log(options_.info_log, "Checking %s, keep: %d", filename.c_str(), keep);
          break;
      }

      if (!keep) {
        files_to_delete.push_back(std::move(filename));
        if (type == kTableFile) {
          table_cache_->Evict(number);
        }
        // Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),
            // static_cast<unsigned long long>(number));
      }else{
        // Log(options_.info_log, "Unrecognized file name: %s", filename.c_str());
      }
    }
  }

  // While deleting all files unblock other threads. All files being deleted
  // have unique names which will not collide with newly created files and
  // are therefore safe to delete while allowing other threads to proceed.
  mutex_.Unlock();
  for (const std::string& filename : files_to_delete) {
    Log(options_.info_log, "Removing file: %s", filename.c_str());
    env_->RemoveFile(dbname_ + "/" + filename);
  }
  mutex_.Lock();
}

void DBImpl::TieringRemoveObsoleteFiles() {
  mutex_.AssertHeld();

  if (!bg_error_.ok()) {
    // After a background error, we don't know whether a new version may
    // or may not have been committed, so we cannot safely garbage collect.
    return;
  }

  // Make a set of all of the live files
  std::set<uint64_t> live = pending_outputs_;
  versions_->AddLiveFiles(&live, false);

  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  FileType type;
  std::vector<std::string> files_to_delete;
  for (std::string& filename : filenames) {
    if (ParseFileName(filename, &number, &type)) {
      bool keep = true;
      switch (type) {
        case kLogFile:
          keep = ((number >= versions_->LogNumber()) ||
                  (number == versions_->PrevLogNumber()));
          // Log(options_.info_log, "Checking LogFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kDescriptorFile:
          // Keep my manifest file, and any newer incarnations'
          // (in case there is a race that allows other incarnations)
          keep = (number >= versions_->ManifestFileNumber());
          // Log(options_.info_log, "Checking DescriptorFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kTableFile:
          keep = (live.find(number) != live.end());
          // Log(options_.info_log, "Checking TableFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kTempFile:
          // Any temp files that are currently being written to must
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());
          // Log(options_.info_log, "Checking TempFile: #%llu, keep: %d", (unsigned long long)number, keep);
          break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
          keep = true;
          // Log(options_.info_log, "Checking %s, keep: %d", filename.c_str(), keep);
          break;
      }

      if (!keep) {
        files_to_delete.push_back(std::move(filename));
        if (type == kTableFile) {
          table_cache_->Evict(number);
        }
        // Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),
            // static_cast<unsigned long long>(number));
      }else{
        // Log(options_.info_log, "Unrecognized file name: %s", filename.c_str());
      }
    }
  }

  // While deleting all files unblock other threads. All files being deleted
  // have unique names which will not collide with newly created files and
  // are therefore safe to delete while allowing other threads to proceed.
  mutex_.Unlock();
  for (const std::string& filename : files_to_delete) {
    Log(options_.info_log, "Removing file: %s", filename.c_str());
    env_->RemoveFile(dbname_ + "/" + filename);
  }
  mutex_.Lock();
}

Status DBImpl::Recover(VersionEdit* edit, bool* save_manifest) {
  mutex_.AssertHeld();

  // Ignore error from CreateDir since the creation of the DB is
  // committed only when the descriptor is created, and this directory
  // may already exist from a previous failed creation attempt.
  env_->CreateDir(dbname_);
  assert(db_lock_ == nullptr);
  Status s = env_->LockFile(LockFileName(dbname_), &db_lock_);
  if (!s.ok()) {
    return s;
  }

  
  if (!env_->FileExists(CurrentFileName(dbname_))) {
    if (options_.create_if_missing) {
      Log(options_.info_log, "Creating DB %s since it was missing.",
          dbname_.c_str());
      s = NewDB();
      if (!s.ok()) {
        return s;
      }
    } else {
      return Status::InvalidArgument(
          dbname_, "does not exist (create_if_missing is false)");
    }
  } else {
    if (options_.error_if_exists) {
      return Status::InvalidArgument(dbname_,
                                     "exists (error_if_exists is true)");
    }
  }

  s = RestoreMemPartitionsFromFile(dbname_);
  if (!s.ok()) {
    std::fprintf(stderr, "Not find Partition file: %s\n", s.ToString().c_str());
  }

  s = RestoreHotRangesFromFile(dbname_);
  if (!s.ok()) {
    std::fprintf(stderr, "Not find range file: %s\n", s.ToString().c_str());
  }

  s = versions_->Recover(save_manifest);
  if (!s.ok()) {
    return s;
  }
  SequenceNumber max_sequence(0);
  fprintf(stderr, "After restoring,  save_manifest = %s\n", save_manifest ? "true" : "false");


  // Recover from all newer log files than the ones named in the
  // descriptor (new log files may have been added by the previous
  // incarnation without registering them in the descriptor).
  //
  // Note that PrevLogNumber() is no longer used, but we pay
  // attention to it in case we are recovering a database
  // produced by an older version of leveldb.
  const uint64_t min_log = versions_->LogNumber();
  const uint64_t prev_log = versions_->PrevLogNumber();
  std::vector<std::string> filenames;
  s = env_->GetChildren(dbname_, &filenames);
  if (!s.ok()) {
    return s;
  }
  fprintf(stderr,"we got %ld files !\n", filenames.size());
  std::set<uint64_t> expected;
  versions_->AddLiveFiles(&expected, true);
  fprintf(stderr,"we got %ld live files !\n", expected.size());
  uint64_t number;
  FileType type;
  std::vector<uint64_t> logs;
  for (size_t i = 0; i < filenames.size(); i++) {
    if (ParseFileName(filenames[i], &number, &type)) {
      expected.erase(number);
      if (type == kLogFile && ((number >= min_log) || (number == prev_log))){
        logs.push_back(number);
        fprintf(stderr,"find %ld as a log file!\n", number);
      }
        
    }
  }

  if (!expected.empty()) {
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%d missing files; e.g.",
                  static_cast<int>(expected.size()));
    return Status::Corruption(buf, TableFileName(dbname_, *(expected.begin())));
  }

  // Recover in the order in which the logs were generated
  std::sort(logs.begin(), logs.end());
  for (size_t i = 0; i < logs.size(); i++) {
    s = RecoverLogFile(logs[i], (i == logs.size() - 1), save_manifest, edit,
                       &max_sequence);
    if (!s.ok()) {
      return s;
    }

    // The previous incarnation may not have written any MANIFEST
    // records after allocating this log number.  So we manually
    // update the file number allocation counter in VersionSet.
    versions_->MarkFileNumberUsed(logs[i]);
  }

  if (versions_->LastSequence() < max_sequence) {
    versions_->SetLastSequence(max_sequence);
  }

  return Status::OK();
}

Status DBImpl::RecoverLogFile(uint64_t log_number, bool last_log,
                              bool* save_manifest, VersionEdit* edit,
                              SequenceNumber* max_sequence) {
  struct LogReporter : public log::Reader::Reporter {
    Env* env;
    Logger* info_log;
    const char* fname;
    Status* status;  // null if options_.paranoid_checks==false
    void Corruption(size_t bytes, const Status& s) override {
      Log(info_log, "%s%s: dropping %d bytes; %s",
          (this->status == nullptr ? "(ignoring error) " : ""), fname,
          static_cast<int>(bytes), s.ToString().c_str());
      if (this->status != nullptr && this->status->ok()) *this->status = s;
    }
  };

  mutex_.AssertHeld();

  // Open the log file
  std::string fname = LogFileName(dbname_, log_number);
  SequentialFile* file;
  Status status = env_->NewSequentialFile(fname, &file);
  if (!status.ok()) {
    MaybeIgnoreError(&status);
    return status;
  }

  // Create the log reader.
  LogReporter reporter;
  reporter.env = env_;
  reporter.info_log = options_.info_log;
  reporter.fname = fname.c_str();
  reporter.status = (options_.paranoid_checks ? &status : nullptr);
  // We intentionally make log::Reader do checksumming even if
  // paranoid_checks==false so that corruptions cause entire commits
  // to be skipped instead of propagating bad information (like overly
  // large sequence numbers).
  log::Reader reader(file, &reporter, true /*checksum*/, 0 /*initial_offset*/);
  Log(options_.info_log, "Recovering log #%llu",
      (unsigned long long)log_number);

  // Read all the records and add to a memtable
  std::string scratch;
  Slice record;
  WriteBatch batch;
  int compactions = 0;
  MemTable* mem = nullptr;
  MemTable* hot_mem = nullptr;

  while (reader.ReadRecord(&record, &scratch) && status.ok()) {
    if (record.size() < 12) {
      reporter.Corruption(record.size(),
                          Status::Corruption("log record too small"));
      continue;
    }
    WriteBatchInternal::SetContents(&batch, record);

    if (mem == nullptr) {
      mem = new MemTable(internal_comparator_);
      mem->Ref();
    }

    if (hot_mem == nullptr) {
      hot_mem = new MemTable(internal_comparator_);
      hot_mem->Ref();
    }

    status = WriteBatchInternal::InsertInto(&batch, mem, hot_mem, options_.info_log, HotRanges);
    MaybeIgnoreError(&status);
    if (!status.ok()) {
      break;
    }
    const SequenceNumber last_seq = WriteBatchInternal::Sequence(&batch) +
                                    WriteBatchInternal::Count(&batch) - 1;
    if (last_seq > *max_sequence) {
      *max_sequence = last_seq;
    }

    if (mem->ApproximateMemoryUsage() > options_.write_buffer_size) {
      compactions++;
      *save_manifest = true;
      status = WritePartitionLevelingL0Table(mem, edit, nullptr);
      mem->Unref();
      mem = nullptr;
      if (!status.ok()) {
        // Reflect errors immediately so that conditions like full
        // file-systems cause the DB::Open() to fail.
        break;
      }
    }


    if (hot_mem->ApproximateMemoryUsage() > options_.write_hot_buffer_size) {
      compactions++;
      *save_manifest = true;
      status = WriteLevel0Table(hot_mem, edit, nullptr);
      hot_mem->Unref();
      hot_mem = nullptr;
      if (!status.ok()) {
        // Reflect errors immediately so that conditions like full
        // file-systems cause the DB::Open() to fail.
        break;
      }
      edit->set_is_tiering();
    }

  }

  delete file;

  // See if we should keep reusing the last log file.
  if (status.ok() && options_.reuse_logs && last_log && compactions == 0) {
    assert(logfile_ == nullptr);
    assert(log_ == nullptr);
    assert(mem_ == nullptr);
    uint64_t lfile_size;
    if (env_->GetFileSize(fname, &lfile_size).ok() &&
        env_->NewAppendableFile(fname, &logfile_).ok()) {
      Log(options_.info_log, "Reusing old log %s \n", fname.c_str());
      log_ = new log::Writer(logfile_, lfile_size);
      logfile_number_ = log_number;
      if (mem != nullptr) {
        mem_ = mem;
        mem = nullptr;
      } else {
        // mem can be nullptr if lognum exists but was empty.
        mem_ = new MemTable(internal_comparator_);
        mem_->Ref();
      }
    }
  }

  if (mem != nullptr) {
    // mem did not get reused; compact it.
    if (status.ok()) {
      *save_manifest = true;
      status = WritePartitionLevelingL0Table(mem, edit, nullptr);
    }
    mem->Unref();
  }

  return status;
}

Status DBImpl::WriteLevel0Table(MemTable* mem, VersionEdit* edit,
                                Version* base) {
  mutex_.AssertHeld();
  const uint64_t start_micros = env_->NowMicros();

  FileMetaData meta;
  meta.number = versions_->NewFileNumber();
  // 设置 logger
  edit->set_logger(options_.info_log);

  pending_outputs_.insert(meta.number); 
  Iterator* iter = mem->NewIterator();
  
 
  Log(options_.info_log,
    "Level-0 Tiering: Table #%llu minor compaction - Started",
    (unsigned long long)meta.number);
  

  Status s;
  {
    mutex_.Unlock();
    s = BuildTable(dbname_, env_, options_, table_cache_, iter, &meta);
    mutex_.Lock();
  }

 
  Log(options_.info_log,
    "Level-0 Tiering: Table #%llu, Size: %lld bytes, Status: %s",
    (unsigned long long)meta.number,
    (unsigned long long)meta.file_size,
    s.ToString().c_str());
  

  delete iter;
  pending_outputs_.erase(meta.number);

  // Note that if file_size is zero, the file has been deleted and
  // should not be added to the manifest.
  int level = 0;
  if (s.ok() && meta.file_size > 0) {
    const Slice min_user_key = meta.smallest.user_key();
    const Slice max_user_key = meta.largest.user_key();

    // level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
    edit->AddTieringFile(level, 0, meta.number, meta.file_size, meta.smallest, meta.largest);
    
  }

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros;
  stats.bytes_written = meta.file_size;
  stats_[level].Add(stats);


  // newly added source codes
  level_stats_[0].micros = env_->NowMicros() - start_micros;
  level_stats_[0].user_bytes_written = meta.file_size;
  level_stats_[0].num_tiering_files++;
  level_stats_[0].tiering_bytes_written += meta.file_size;
  
  
  return s;
}

void DBImpl::CompactTieringMemTable() {
  mutex_.AssertHeld();

  // Compact the hot immutable memtable
  VersionEdit hot_edit;
  Version* hot_base = versions_->current();
  hot_base->Ref();
  Status hot_s = WriteLevel0Table(hot_imm_, &hot_edit, hot_base);
  hot_base->Unref();

  if (hot_s.ok() && shutting_down_.load(std::memory_order_acquire)) {
    hot_s = Status::IOError("Deleting DB during hot memtable compaction");
  }

  if (hot_s.ok()) {
    hot_edit.SetPrevLogNumber(0);
    hot_edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
    hot_edit.set_is_tiering();
    hot_s = versions_->LogAndApply(&hot_edit, &mutex_);
  }

  if (hot_s.ok()) {
    // Commit to the new state
    hot_imm_->Unref();
    hot_imm_ = nullptr;
    has_hot_imm_.store(false, std::memory_order_release);
  } else {
    RecordBackgroundError(hot_s);
  }
  TieringRemoveObsoleteFiles();
}


Status DBImpl::WriteLevel0Table_NoPartition(MemTable* mem, VersionEdit* edit, Version* base) {
  mutex_.AssertHeld();
  const uint64_t start_micros = env_->NowMicros();

  FileMetaData meta;
  meta.number = versions_->NewFileNumber();
  // 设置 logger
  edit->set_logger(options_.info_log);


  pending_outputs_.insert(meta.number); 
  Iterator* iter = mem->NewIterator();
  
  Log(options_.info_log,
    "Level-0 Leveling: Table #%llu minor compaction - Started",
    (unsigned long long)meta.number);
  

  Status s;
  {
    mutex_.Unlock();
    s = BuildTable(dbname_, env_, options_, table_cache_, iter, &meta);
    mutex_.Lock();
  }


  Log(options_.info_log,
    "Level-0 Leveling: Table #%llu, Size: %lld bytes, Status: %s",
    (unsigned long long)meta.number,
    (unsigned long long)meta.file_size,
    s.ToString().c_str());
  

  delete iter;
  pending_outputs_.erase(meta.number);

  // Note that if file_size is zero, the file has been deleted and
  // should not be added to the manifest.
  int level = 0;
  if (s.ok() && meta.file_size > 0) {
    const Slice min_user_key = meta.smallest.user_key();
    const Slice max_user_key = meta.largest.user_key();
     if(base!= nullptr){
      level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
      edit->AddFile(level, meta.number, meta.file_size, meta.smallest, meta.largest);
    }
  }

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros;
  stats.bytes_written = meta.file_size;
  stats_[level].Add(stats);

  // newly added source codes
  level_stats_[0].micros = env_->NowMicros() - start_micros;
  level_stats_[0].user_bytes_written = meta.file_size;
  level_stats_[0].num_leveling_files++;
  level_stats_[0].leveling_bytes_written += meta.file_size;
  
  return s;
}

void DBImpl::CompactMemTable() {
  mutex_.AssertHeld();
  assert(imm_ != nullptr || hot_imm_ != nullptr);

  // Compact the regular immutable memtable
  VersionEdit edit;
  Version* base = versions_->current();
  base->Ref();
  Status s = WriteLevel0Table_NoPartition(imm_, &edit, base);
  base->Unref();

  if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
    s = Status::IOError("Deleting DB during memtable compaction");
  }

  if (s.ok()) {
    edit.SetPrevLogNumber(0);
    edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
    s = versions_->LogAndApply(&edit, &mutex_);
  }

  if (s.ok()) {
    // Commit to the new state
    imm_->Unref();
    imm_ = nullptr;
    has_imm_.store(false, std::memory_order_release);
  } else {
    RecordBackgroundError(s);
  }
}

void PrintPartitionFiles(const std::vector<std::pair<uint64_t, FileMetaData*>>& partition_files) {
  for (const auto& partition_file : partition_files) {
    uint64_t partition_start = partition_file.first;
    FileMetaData* file_meta = partition_file.second;
    fprintf(stderr, "Partition number: %lu | File start: %s | File number: %lu | File size: %lu bytes | Smallest key: %s | Largest key: %s\n", 
      partition_start, file_meta->smallest.DebugString().c_str(), file_meta->number, file_meta->file_size, 
      file_meta->smallest.DebugString().c_str(), file_meta->largest.DebugString().c_str());
    }
}

void PrintPartitionAndFileInfo(const mem_partition_guard* current_partition, const FileMetaData* file_meta) {
  fprintf(stderr, "============================================\n");
  fprintf(stderr, "Partition Information:\n");
  fprintf(stderr, "  Partition number: %llu\n", (unsigned long long)current_partition->partition_num);
  fprintf(stderr, "\n");
  fprintf(stderr, "File Metadata Information:\n");
  fprintf(stderr, "  File number: %llu\n", (unsigned long long)file_meta->number);
  fprintf(stderr, "  File size: %llu bytes\n", (unsigned long long)file_meta->file_size);
  fprintf(stderr, "  Smallest key: %s\n", file_meta->smallest.DebugString().c_str());
  fprintf(stderr, "  Largest key: %s\n", file_meta->largest.DebugString().c_str());
  fprintf(stderr, "============================================\n");
}

void PrintPartitions(const std::set<mem_partition_guard*, PartitionGuardComparator>& mem_partitions) {

  fprintf(stderr, "============================================\n");
  for (const auto& partition : mem_partitions) {
    
    fprintf(stderr, "Partition: %4lu | Start: %-20s | End: %-20s | Total files: %5lu | Written KVs: %10lu | Total file size: %10lu bytes | Avg file size: %10lu bytes\n",
      partition->partition_num, 
      partition->partition_start_str.c_str(), 
      partition->partition_end_str.c_str(), 
      partition->total_files, 
      partition->written_kvs,
      partition->total_file_size, 
      partition->GetAverageFileSize());
    for (const auto& sub_partition : partition->sub_partitions_){
      fprintf(stderr, "     Partition: %4lu | Start: %-20s | End: %-20s | Total files: %5lu | Written KVs: %10lu | Total file size: %10lu bytes | Avg file size: %10lu bytes\n",
        sub_partition->partition_num, 
        sub_partition->partition_start_str.c_str(), 
        sub_partition->partition_end_str.c_str(), 
        sub_partition->total_files, 
        sub_partition->written_kvs,
        sub_partition->total_file_size, 
        sub_partition->GetAverageFileSize());
    }
  }
  fprintf(stderr, "============================================\n\n\n");
}

void PrintPartition(mem_partition_guard* partition) {

  if (partition == nullptr) {
    fprintf(stderr, "Partition: nullptr\n");
    return;
  }

  fprintf(stderr, "Partition: %4lu | Start: %-20s | End: %-20s | Total files: %5lu | Written KVs: %10lu | Total file size: %10lu bytes | Avg file size: %10lu bytes\n",
    partition->partition_num, 
    partition->partition_start_str.c_str(), 
    partition->partition_end_str.c_str(), 
    partition->total_files, 
    partition->written_kvs,
    partition->total_file_size, 
    partition->GetAverageFileSize());
}

Status DBImpl::AddDataIntoPartitions(Iterator* iter, const Options& options,TableCache* table_cache,
                                    std::vector<std::pair<uint64_t, FileMetaData*>>& partition_files, uint64_t mem_size){

  Status s;
  iter->SeekToFirst();
  Slice add_key;
  int end_partition = 0;

  FileMetaData* file_meta = new FileMetaData();
  file_meta->number = versions_->NewFileNumber();
  pending_outputs_.insert(file_meta->number); 

  // fprintf(stderr, "New file(%lu) was created!\n", file_meta->number);
  std::string fname = TableFileName(dbname_, file_meta->number);
  WritableFile* file;
  s = env_->NewWritableFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  TableBuilder* builder = new TableBuilder(options, file);

  const char* current_key_pointer = iter->key().data();
  size_t current_key_size = iter->key().size()-8;

  int64_t start_micros = env_->NowMicros();
  file_meta->smallest.DecodeFrom(iter->key()); 

  mem_partition_guard* current_partition = nullptr;
  mem_partition_guard* next_partition = nullptr;

  std::string current_key_str(current_key_pointer, current_key_size);
  mem_partition_guard temp_partition(current_key_str, current_key_str);

  bool is_last_expand = false;
    
  // 使用 upper_bound 查找第一个大于 temp_partition 的 partition
  auto it = mem_partitions_.upper_bound(&temp_partition);
  if(it == mem_partitions_.begin()){
    current_partition = *it;
    it++;
    assert(current_partition->CompareWithBegin(current_key_pointer, current_key_size) > 0);

    if(current_partition->GetAverageFileSize() < options.min_file_size){
      std::string new_start_str(current_key_pointer,current_key_size);
      current_partition->UpdatePartitionStart(new_start_str);
    }else{
      std::string new_start_str(current_key_pointer,current_key_size);
      current_partition = CreateAndInsertPartition(new_start_str, new_start_str, versions_->NewPartitionNumber(), mem_partitions_);
      partition_first_L0flush_map_[current_partition->partition_num] = true;
      Log(options_.leveling_info_log,"[DEBUG] current_partition->partition_num: %lu",
        current_partition->partition_num);
    }

  }else{
    --it;
    current_partition = *it;
  }

  GetNextPartition(mem_partitions_, current_partition, next_partition);
  // fprintf(stderr, "Partition start: %s, end: %s\n", (*it)->partition_start.ToString().c_str(), (*it)->partition_end.ToString().c_str());

  uint64_t wrritten_sizes = 0;
  for (; iter->Valid(); iter->Next()) {

    current_key_pointer = iter->key().data();
    current_key_size = iter->key().size();
    current_key_size -= 8;

    if(current_partition->CompareWithEnd(current_key_pointer, current_key_size ) < 0){

      // that means 'current' mem_partition is a newly created partition!
      // for a newly created partition, we must need to expand the partition size because of original partition length is 1
      if(builder->FileSize() < options.min_file_size && CanIncreasePartition(mem_partitions_, current_partition, next_partition, current_key_pointer, current_key_size)){
        add_key = iter->key();
        builder->Add(add_key, iter->value());
        std::string new_end_str(current_key_pointer, current_key_size );
        current_partition->UpdatePartitionEnd(new_end_str);
        is_last_expand = true;
        continue;
      }

      if(builder->NumEntries() <=100){
        if(CanIncreasePartitionStart(mem_partitions_, current_partition, next_partition, current_key_pointer, current_key_size)){
          add_key = iter->key();
          builder->Add(add_key, iter->value());
          std::string new_start_str = current_partition->partition_start_str;
          // change the position of pointer!
          RemovePartition(mem_partitions_, current_partition);
          fprintf(stderr,"before update: start str:%s\n",next_partition->partition_start_str.c_str());
          next_partition->UpdatePartitionStart(new_start_str);
          current_partition = next_partition;
          fprintf(stderr,"After update: start str:%s\n",current_partition->partition_start_str.c_str());
          GetNextPartition(mem_partitions_, current_partition, next_partition);
          continue;
        } 
      }

      // if(next_partition == nullptr){
      //   wrritten_sizes += builder->FileSize();
      //   uint64_t remain_size = mem_size - wrritten_sizes;
      //   fprintf(stderr,"mem_size %lu wrritten_sizes %lu remain_size: %lu \n",mem_size,wrritten_sizes, remain_size);
      //   if(remain_size<1024){
      //     while(!iter->Valid()){
      //       add_key = iter->key();
      //       builder->Add(add_key, iter->value());
      //       iter->Next();
      //     }
      //   }
      // }
      s = builder->Finish();
      if (s.ok()) {
        file_meta->file_size = builder->FileSize();
        assert(file_meta->file_size > 0);
        // wrritten_sizes += builder->FileSize();
      }
      file_meta->largest.DecodeFrom(add_key);
      current_partition->Add_File(file_meta->file_size, builder->NumEntries());
      delete builder;
      if (s.ok()) {
        s = file->Sync();
      }
      if (s.ok()) {
        s = file->Close();
      }
      delete file;
      file = nullptr;

      if (s.ok()) {
        // Verify that the table is usable
        Iterator* it = table_cache->NewIterator(ReadOptions(), file_meta->number, file_meta->file_size);
        s = it->status();
        delete it;
      }


      if(is_last_expand){
        std::string new_end_str(add_key.data(), current_key_size);
        Slice new_end_slice(new_end_str);
        if(current_partition->partition_end.compare(new_end_slice)<0){
          fprintf(stderr,"before update: end str:%s\n",current_partition->partition_end_str.c_str());
          current_partition->UpdatePartitionEnd(new_end_str);
          fprintf(stderr,"after update:  end str:%s\n",current_partition->partition_end_str.c_str());
        }
        is_last_expand = false;
      }

      partition_files.emplace_back(current_partition->partition_num, file_meta);
      // PrintPartitionAndFileInfo(current_partition, file_meta);

      current_partition = nullptr;
      file_meta = nullptr;      

      std::string temp_key_str(current_key_pointer, current_key_size);
      mem_partition_guard temp_partition(temp_key_str, temp_key_str);
      it = mem_partitions_.upper_bound(&temp_partition);
      if(it == mem_partitions_.end()){
        it--;
        if((*it)->CompareWithEnd(current_key_pointer,current_key_size)<0){
          std::string new_start_key_str(current_key_pointer, current_key_size);
          current_partition = CreateAndInsertPartition(new_start_key_str, new_start_key_str, versions_->NewPartitionNumber(), mem_partitions_);
          partition_first_L0flush_map_[current_partition->partition_num] = true;
          Log(options_.leveling_info_log,"[DEBUG] current_partition->partition_num: %lu",
            current_partition->partition_num);
          end_partition++;
        }else{
          current_partition = *it;
        }
      }else{
        assert(it != mem_partitions_.begin());
        it--;
        current_partition = *it;
        if(current_partition->CompareWithEnd(current_key_pointer, current_key_size) < 0){
          it++;
          current_partition = *it;
          if(current_partition->GetAverageFileSize() < options.min_file_size){
            current_partition->UpdatePartitionStart(temp_key_str);

          }else{
            current_partition = CreateAndInsertPartition(temp_key_str, temp_key_str, versions_->NewPartitionNumber(), mem_partitions_);
            partition_first_L0flush_map_[current_partition->partition_num] = true;
            Log(options_.leveling_info_log,"[DEBUG] current_partition->partition_num: %lu",
              current_partition->partition_num);
          }
        }else{
          assert(current_partition->CompareWithBegin(current_key_pointer, current_key_size) <= 0);
        }
      }

      GetNextPartition(mem_partitions_, current_partition, next_partition);

      file_meta = new FileMetaData();
      file_meta->number = versions_->NewFileNumber();

      pending_outputs_.insert(file_meta->number);
      // fprintf(stderr, "New file(%lu) was created!\n", file_meta->number);
      fname = TableFileName(dbname_, file_meta->number);
      s = env_->NewWritableFile(fname, &file);
      if (!s.ok()) {
        return s;
      }
      builder = new TableBuilder(options, file);
      add_key = iter->key();
      builder->Add(add_key, iter->value());
      file_meta->smallest.DecodeFrom(add_key);
      continue;
    }
    
    add_key = iter->key();
    builder->Add(add_key, iter->value());
  }

  if(current_partition != nullptr && file_meta != nullptr){
    s = builder->Finish();
    if(!add_key.empty()){
      file_meta->largest.DecodeFrom(add_key);
      if(is_last_expand){
        std::string new_end_str(add_key.data(), add_key.size()-8);
        Slice new_end_slice(new_end_str);
        if(current_partition->partition_end.compare(new_end_slice)<0){
          fprintf(stderr,"before update: end str:%s\n",current_partition->partition_end_str.c_str());
          current_partition->UpdatePartitionEnd(new_end_str);
          fprintf(stderr,"after update:  end str:%s\n",current_partition->partition_end_str.c_str());
        }
      }
    }
    if (s.ok()) {
      file_meta->file_size = builder->FileSize();
      assert(file_meta->file_size > 0);
    }
    current_partition->Add_File(file_meta->file_size, builder->NumEntries());
    delete builder;
    if (s.ok()) {
      s = file->Sync();
    }
    if (s.ok()) {
      s = file->Close();
    }
    delete file;
    file = nullptr;

    if (s.ok()) {
      // Verify that the table is usable
      Iterator* it = table_cache->NewIterator(ReadOptions(), file_meta->number, file_meta->file_size);
      s = it->status();
      delete it;
    }
    partition_files.emplace_back(current_partition->partition_num, file_meta);
    // PrintPartitionAndFileInfo(current_partition, file_meta);
  }

  // Check for input iterator errors
  if (!iter->status().ok()) {
    s = iter->status();
  }


  // fprintf(stderr,"we created %d partitions because of ending the set!\n",end_partition);
  // PrintPartitions(mem_partitions_);
  return s;
}

uint64_t UpdateSubPartitionsEnd(leveldb::mem_partition_guard* partition, const std::string& new_end) {
  partition->UpdatePartitionEnd(new_end);
  if (!partition->sub_partitions_.empty()) {
    auto it = partition->sub_partitions_.end();
    it--;
    (*it)->UpdatePartitionEnd(new_end);
    return (*it)->partition_num;
  }
  return 0;
}

uint64_t UpdateSubPartitionsStart(leveldb::mem_partition_guard* partition, const std::string& new_start) {
  partition->UpdatePartitionStart(new_start);
  if (!partition->sub_partitions_.empty()) {
    auto it = partition->sub_partitions_.begin();
    (*it)->UpdatePartitionStart(new_start);
    return (*it)->partition_num;
  }
  
  return 0;
}

void DBImpl::Merge_all_supersmall_partitions( std::vector<uint64_t>& merge_deleted_partition_nums) {
  std::vector<mem_partition_guard*> to_merge;
  mem_partition_guard* first_partition = nullptr;
  mem_partition_guard* last_partition = nullptr;

  mem_partition_guard* prev_first_partition = nullptr;
  mem_partition_guard* suces_last_partition = nullptr;
  mem_partition_guard* merge_target = nullptr;

  uint64_t total_average_file_size = 0;
  uint64_t need_merge_to_partition;

  auto it = mem_partitions_.begin();
  while (it != mem_partitions_.end()) {
    mem_partition_guard* partition = *it;
    if (partition->GetAverageFileSize() < 1024) {
      total_average_file_size += partition->GetAverageFileSize();
      if (first_partition == nullptr) {
        first_partition = partition;
        prev_first_partition = (it == mem_partitions_.begin()) ? nullptr : *(std::prev(it));
      }
      last_partition = partition;
      to_merge.push_back(partition);
      Log(options_.leveling_info_log, "Marked for merge: partition[%lu] start=%s, partition_end=%s, average_file_size=%llu",
          partition->partition_num, partition->partition_start.ToString().c_str(),
          partition->partition_end.ToString().c_str(),
          (unsigned long long)partition->GetAverageFileSize());
      merge_deleted_partition_nums.emplace_back((*it)->partition_num);
      it = mem_partitions_.erase(it);
    } else {
      if (!to_merge.empty()) {
        bool merge_with_prev = false;
        if (total_average_file_size / to_merge.size() < 512*1024) {
          suces_last_partition = (it == mem_partitions_.end()) ? nullptr : *it;
          if (prev_first_partition && suces_last_partition) {
            merge_with_prev = prev_first_partition->GetAverageFileSize() < suces_last_partition->GetAverageFileSize();
          } else if (prev_first_partition) {
            merge_with_prev = true;
          }
        }

        if (merge_with_prev) {
          need_merge_to_partition = UpdateSubPartitionsEnd(prev_first_partition, last_partition->partition_end.ToString());
          for (auto& p : to_merge) {
            if (p != last_partition) {
              delete p;
              partition_first_L0flush_map_.erase(p->partition_num); 
              fprintf(stdout,"delete partition %lu\n",p->partition_num);
            }
          }
        } else if (suces_last_partition) {
          need_merge_to_partition = UpdateSubPartitionsStart(suces_last_partition, first_partition->partition_start.ToString());
          for (auto& p : to_merge) {
            if (p != first_partition) {
              delete p;
              partition_first_L0flush_map_.erase(p->partition_num); // 移除被删除的 partition
              fprintf(stdout,"delete partition %lu\n",p->partition_num);
            }
          }
        } else {
          std::string new_start = first_partition->partition_start.ToString();
          std::string new_end = last_partition->partition_end.ToString();
          mem_partition_guard* new_partition = new leveldb::mem_partition_guard(new_start, new_end);
          new_partition->partition_num = versions_->NewPartitionNumber();
          need_merge_to_partition = new_partition->partition_num;
          Log(options_.leveling_info_log, "Creating new merged partition: new_start=%s, new_end=%s, partition_num=%llu",
            new_start.c_str(), new_end.c_str(), (unsigned long long)new_partition->partition_num);

          for (mem_partition_guard* p : to_merge) {
            Log(options_.leveling_info_log, "Deleting old partition: partition_start=%s, partition_end=%s",
              p->partition_start.ToString().c_str(), p->partition_end.ToString().c_str());
            partition_first_L0flush_map_.erase(p->partition_num); // 移除被删除的 partition
            fprintf(stdout,"delete partition %lu\n",p->partition_num);
            delete p;
          }
          mem_partitions_.insert(new_partition);
        }
        
        // Reset for next merge
        merge_deleted_partition_nums.emplace_back(need_merge_to_partition);
        to_merge.clear();
        first_partition = nullptr;
        last_partition = nullptr;
        total_average_file_size = 0;
        return ;
      }
      ++it;
    }
  }

  
  // If there are remaining partitions to merge after the loop
  if (!to_merge.empty()) {
    
    bool merge_with_prev = false;
    if (total_average_file_size / to_merge.size() < 512*1024) {
      suces_last_partition = (it == mem_partitions_.end()) ? nullptr : *it;
      if (prev_first_partition && suces_last_partition) {
          merge_with_prev = prev_first_partition->GetAverageFileSize() < suces_last_partition->GetAverageFileSize();
      } else if (prev_first_partition) {
          merge_with_prev = true;
      }
    }

    if (merge_with_prev) {
      need_merge_to_partition = UpdateSubPartitionsEnd(prev_first_partition, last_partition->partition_end.ToString());
      for (mem_partition_guard* p : to_merge) { 
        partition_first_L0flush_map_.erase(p->partition_num);
        fprintf(stdout,"delete partition %lu\n",p->partition_num);
        delete p;
      }
    } else if (suces_last_partition) {
      need_merge_to_partition = UpdateSubPartitionsStart(suces_last_partition, first_partition->partition_start.ToString());
      for (mem_partition_guard* p : to_merge) {
        partition_first_L0flush_map_.erase(p->partition_num); 
        fprintf(stdout,"delete partition %lu\n",p->partition_num);
        delete p;
      }
    } else {
      std::string new_start = first_partition->partition_start.ToString();
      std::string new_end = last_partition->partition_end.ToString();
      mem_partition_guard* new_partition = new mem_partition_guard(new_start, new_end);
      new_partition->partition_num = versions_->NewPartitionNumber();
      partition_first_L0flush_map_[new_partition->partition_num] = false;
      fprintf(stdout,"create a new partition %lu\n",new_partition->partition_num);
      Log(options_.leveling_info_log, "Creating final merged partition: new_start=%s, new_end=%s, partition_num=%llu",
          new_start.c_str(), new_end.c_str(), (unsigned long long)new_partition->partition_num);

      for (mem_partition_guard* p : to_merge) {
        Log(options_.leveling_info_log, "Deleting old partition: partition_start=%s, partition_end=%s",
            p->partition_start.ToString().c_str(), p->partition_end.ToString().c_str());
        partition_first_L0flush_map_.erase(p->partition_num);
        fprintf(stdout,"delete partition %lu\n",p->partition_num); 
        delete p;
      }
      mem_partitions_.insert(new_partition);
    }
  }

  if(need_merge_to_partition != 0){
    merge_deleted_partition_nums.emplace_back(need_merge_to_partition);
  }
  
  PrintPartitions(mem_partitions_);

  return ;
}




Status DBImpl::CreatePartitions(Iterator* iter, const Options& options, TableCache* table_cache, 
                                  std::vector<std::pair<uint64_t,FileMetaData*>>& partition_files) {

  Status s;
  iter->SeekToFirst();

  size_t add_key_size = 0;

  FileMetaData* file_meta = new FileMetaData();
  file_meta->file_size = 0;
  file_meta->number = versions_->NewFileNumber();

  pending_outputs_.insert(file_meta->number); 
  // fprintf(stderr, "New file(%lu) was created!\n", file_meta->number);

  if(!iter->Valid()){
    return Status::Corruption("Empty iterator");
  }

  std::string fname = TableFileName(dbname_, file_meta->number);
  WritableFile* file;
  s = env_->NewWritableFile(fname, &file);
  if (!s.ok()) {
    return s;
  }
  TableBuilder* builder = new TableBuilder(options, file);

  // create the first mem partition
  std::string key_strat_str = std::string(iter->key().data(), iter->key().size()-8);
  mem_partition_guard* current_partition = new mem_partition_guard(key_strat_str, key_strat_str);

  uint64_t now_partition_number = versions_->NewPartitionNumber();
  current_partition->partition_num = now_partition_number;
  partition_first_L0flush_map_[current_partition->partition_num] = true;


  Log(options_.leveling_info_log,
    "[DEBUG] now_partition_number: %lu, current_partition->partition_num: %lu",
    now_partition_number, current_partition->partition_num);

  Log(options_.leveling_info_log,
    "Level-0 Leveling: partition:%lu, Table #%llu minor compaction - Started",
    current_partition->partition_num, (unsigned long long)file_meta->number);

  const char* current_key_pointer;
  bool is_last_expand = false;
  size_t current_key_size;
  int64_t start_micros = env_->NowMicros();
  file_meta->smallest.DecodeFrom(iter->key()); 
  Slice add_key;

  for (; iter->Valid(); iter->Next()) {
    current_key_pointer = iter->key().data();
    current_key_size = iter->key().size();

    // std::string truncated_key(current_key_pointer, current_key_size - 8);
    // fprintf(stderr, "current key: %s key size: %ld\n", truncated_key.c_str(), current_key_size);

    if(current_partition->CompareWithEnd(current_key_pointer, current_key_size - 8) < 0){
      if(builder->FileSize()<options.min_file_size){
        std::string new_end_str(current_key_pointer, current_key_size - 8);
        current_partition->UpdatePartitionEnd(new_end_str);
        add_key = iter->key();
        builder->Add(add_key, iter->value());
        continue;
      }

      s = builder->Finish();
      if (s.ok()) {
        file_meta->file_size = builder->FileSize();
        assert(file_meta->file_size > 0);
      }
      // fprintf(stderr, "finishing builder file: builder:%ld file size:%ld\n", builder->FileSize(), file_meta->file_size);
      current_partition->Add_File(file_meta->file_size, builder->NumEntries());
      file_meta->largest.DecodeFrom(add_key);
      delete builder;

      if (s.ok()) {
      s = file->Sync();
      }
      if (s.ok()) {
        s = file->Close();
      }
      delete file;
      file = nullptr;

      if (s.ok()) {
        // Verify that the table is usable
        Iterator* it = table_cache->NewIterator(ReadOptions(), file_meta->number, file_meta->file_size);
        s = it->status();
        delete it;
      }

      
      std::string new_end_str(add_key.data(), add_key.size()-8);
      current_partition->UpdatePartitionEnd(new_end_str);
      partition_files.emplace_back(now_partition_number, file_meta);
      mem_partitions_.insert(current_partition);
      
      current_partition = nullptr;
      file_meta = nullptr;

      std::string new_key_str(current_key_pointer, current_key_size-8);
      current_partition = new mem_partition_guard(new_key_str, new_key_str);
      now_partition_number = versions_->NewPartitionNumber();
      current_partition->partition_num = now_partition_number;
      partition_first_L0flush_map_[current_partition->partition_num] = true;

      Log(options_.leveling_info_log,
        "[DEBUG] now_partition_number: %lu, current_partition->partition_num: %lu",
        now_partition_number, current_partition->partition_num);

      // print info of newly created mem_partition_guard 
      file_meta = new FileMetaData();
      file_meta->number = versions_->NewFileNumber();

      pending_outputs_.insert(file_meta->number);
      // fprintf(stderr, "New file(%lu) was created!\n", file_meta->number);
      fname = TableFileName(dbname_, file_meta->number);
      s = env_->NewWritableFile(fname, &file);
      if (!s.ok()) {
        return s;
      }
      builder = new TableBuilder(options, file);
      add_key = iter->key();
      builder->Add(add_key, iter->value());
      file_meta->smallest.DecodeFrom(add_key);
      continue;
    }
    
    add_key = iter->key();
    builder->Add(add_key, iter->value());
  }

  if(current_partition != nullptr && file_meta != nullptr){
    if (!add_key.empty()){ 
      fprintf(stderr,"the added key is %s\n", add_key.ToString().c_str());
      file_meta->largest.DecodeFrom(add_key);
      std::string new_end_str(add_key.data(), add_key.size()-8);
      current_partition->UpdatePartitionEnd(new_end_str);
    }
    s = builder->Finish();
    if (s.ok()) {
      file_meta->file_size = builder->FileSize();
      assert(file_meta->file_size > 0);
    }
    current_partition->Add_File(file_meta->file_size, builder->NumEntries());
    delete builder;
    if (s.ok()) {
      s = file->Sync();
    }
    if (s.ok()) {
      s = file->Close();
    }
    delete file;
    file = nullptr;

    if (s.ok()) {
      // Verify that the table is usable
      Iterator* it = table_cache->NewIterator(ReadOptions(), file_meta->number, file_meta->file_size);
      s = it->status();
      delete it;
    }
    mem_partitions_.insert(current_partition);
    partition_files.emplace_back(now_partition_number, file_meta);
  }

  // Check for input iterator errors
  if (!iter->status().ok()) {
    s = iter->status();
  }

  int64_t end_micros = env_->NowMicros();
  PrintPartitions(mem_partitions_);
  return s;
}

Status DBImpl::WritePartitionLevelingL0Table(MemTable* mem, VersionEdit* edit,
                                Version* base) {
  mutex_.AssertHeld();
  const uint64_t start_micros = env_->NowMicros();

  std::vector<std::pair<uint64_t, FileMetaData*>> partition_files;
  edit->set_logger(options_.info_log); // 设置 logger

  Iterator* iter = mem->NewIterator();
  uint64_t total_size_memtable = mem->ApproximateMemoryUsage();

  Status s;
  {
    mutex_.Unlock();
    if(mem_partitions_.size() == 0){
      s = CreatePartitions(iter, options_,table_cache_, partition_files);
    }else{
      s = AddDataIntoPartitions(iter, options_,table_cache_, partition_files, total_size_memtable);
    }
    mutex_.Lock();
  }


  int64_t total_file_size = 0;
  for (const auto& partition_file : partition_files) {
    uint64_t partition_start = partition_file.first;
    FileMetaData* file_meta = partition_file.second;
    Log(options_.leveling_info_log,
      "Level-0 Leveling:Partition:%lu Table #%llu, Size: %lld bytes, Status: %s",partition_start,
      (unsigned long long)file_meta->number,
      (unsigned long long)file_meta->file_size,
      s.ToString().c_str());
    pending_outputs_.erase(file_meta->number);
    total_file_size += file_meta->file_size;
  }
  delete iter;
  

  // Note that if file_size is zero, the file has been deleted and
  // should not be added to the manifest.
  int level = 0;
  if (s.ok() && base!= nullptr) {
    for (const auto& partition_file : partition_files) {
      if(partition_file.second->file_size > 0){
        edit->AddPartitionLevelingFile(partition_file.first, level, partition_file.second->number, 
            partition_file.second->file_size, partition_file.second->smallest, partition_file.second->largest);
      }
    }
  }

  for (auto& partition_file : partition_files) {
    delete partition_file.second;
  }
  partition_files.clear();

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros;
  stats.bytes_written = total_file_size;
  stats_[level].Add(stats);


  // newly added source codes
  level_stats_[level].micros = env_->NowMicros() - start_micros;
  level_stats_[level].user_bytes_written = total_file_size;
  level_stats_[level].num_leveling_files++;
  level_stats_[level].leveling_bytes_written += total_file_size;
  
  
  return s;
}


void DBImpl::CompactLevelingMemTable() {
  mutex_.AssertHeld();
  assert(imm_ != nullptr );
  Status s;
  

  // Compact the regular immutable memtable
  if (imm_ != nullptr) {
    VersionEdit edit;
    Version* base = versions_->current();
    base->Ref();

    s = WritePartitionLevelingL0Table(imm_, &edit, base);

    // PrintPartitionFiles(partition_files);
    // Status s = WriteLevel0Table(imm_, &edit, base, false);

    base->Unref();

    if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
      s = Status::IOError("Deleting DB during memtable compaction");
    }

    if (s.ok()) {
      edit.SetPrevLogNumber(0);
      edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
      s = versions_->LogAndApply(&edit, &mutex_);
    }

    if (s.ok()) {
      // Commit to the new state
      imm_->Unref();
      imm_ = nullptr;
      has_imm_.store(false, std::memory_order_release);
    } else {
      RecordBackgroundError(s);
    }
  }

  RemoveObsoleteFiles();

}

void DBImpl::CompactRange(const Slice* begin, const Slice* end) {
  int max_level_with_files = 1;
  {
    MutexLock l(&mutex_);
    Version* base = versions_->current();
    for (int level = 1; level < config::kNumLevels; level++) {
      if (base->OverlapInLevel(level, begin, end)) {
        max_level_with_files = level;
      }
    }
  }
  TEST_CompactMemTable();  // TODO(sanjay): Skip if memtable does not overlap
  for (int level = 0; level < max_level_with_files; level++) {
    TEST_CompactRange(level, begin, end);
  }
}

void DBImpl::TEST_CompactRange(int level, const Slice* begin,
                               const Slice* end) {
  assert(level >= 0);
  assert(level + 1 < config::kNumLevels);

  InternalKey begin_storage, end_storage;

  ManualCompaction manual;
  manual.level = level;
  manual.done = false;
  if (begin == nullptr) {
    manual.begin = nullptr;
  } else {
    begin_storage = InternalKey(*begin, kMaxSequenceNumber, kValueTypeForSeek);
    manual.begin = &begin_storage;
  }
  if (end == nullptr) {
    manual.end = nullptr;
  } else {
    end_storage = InternalKey(*end, 0, static_cast<ValueType>(0));
    manual.end = &end_storage;
  }

  MutexLock l(&mutex_);
  while (!manual.done && !shutting_down_.load(std::memory_order_acquire) &&
         bg_error_.ok()) {
    if (manual_compaction_ == nullptr) {  // Idle
      manual_compaction_ = &manual;
      MaybeScheduleCompaction();
    } else {  // Running either my compaction or another compaction.
      background_work_finished_signal_.Wait();
    }
  }
  if (manual_compaction_ == &manual) {
    // Cancel my manual compaction since we aborted early for some reason.
    manual_compaction_ = nullptr;
  }
}

Status DBImpl::TEST_CompactMemTable() {
  // nullptr batch means just wait for earlier writes to be done
  Status s = Write(WriteOptions(), nullptr);
  if (s.ok()) {
    // Wait until the compaction completes
    MutexLock l(&mutex_);
    while (imm_ != nullptr && bg_error_.ok()) {
      background_work_finished_signal_.Wait();
    }
    if (imm_ != nullptr) {
      s = bg_error_;
    }
  }
  return s;
}

void DBImpl::RecordBackgroundError(const Status& s) {
  mutex_.AssertHeld();
  if (bg_error_.ok()) {
    bg_error_ = s;
    background_work_finished_signal_.SignalAll();
  }
}

void DBImpl::MaybeScheduleCompaction() {
  mutex_.AssertHeld();
  if (background_compaction_scheduled_) {
    // Already scheduled
  } else if (shutting_down_.load(std::memory_order_acquire)) {
    // DB is being deleted; no more background compactions
  } else if (!bg_error_.ok()) {
    // Already got an error; no more changes
  } else if (imm_ == nullptr && hot_imm_ == nullptr && manual_compaction_ == nullptr &&
             !versions_->NeedsCompaction()) {
    // No work to be done
  } else {
    background_compaction_scheduled_ = true;
    env_->Schedule(&DBImpl::BGWork, this);
  }
}

void DBImpl::BGWork(void* db) {
  reinterpret_cast<DBImpl*>(db)->BackgroundCall();
}

void DBImpl::BackgroundAddData(const Slice& key) {
  hot_key_identifier->add_data(key);
}

void DBImpl::AddDataBGWork(void* arg) {
  auto* data = reinterpret_cast<std::pair<DBImpl*, leveldb::Slice>*>(arg);
  data->first->BackgroundAddData(data->second);
  delete data; // 释放内存
}

void DBImpl::BackgroundCall() {
  MutexLock l(&mutex_);
  assert(background_compaction_scheduled_);
  if (shutting_down_.load(std::memory_order_acquire)) {
    // No more background work when shutting down.
  } else if (!bg_error_.ok()) {
    // No more background work after a background error.
  } else {
    // Log(options_.info_log, "start BackgroundCall!");
    BackgroundCompaction();
  }

  background_compaction_scheduled_ = false;

  // Previous compaction may have produced too many files in a level,
  // so reschedule another compaction if needed.
  MaybeScheduleCompaction();
  background_work_finished_signal_.SignalAll();
}

void DBImpl::BackgroundCompaction() {
  mutex_.AssertHeld();

  Log(options_.info_log, "start background Compaction!");

  if (imm_ != nullptr) {
    Log(options_.leveling_compaction_info_log, "Starting leveling CompactMemTable");
    CompactLevelingMemTable();
    background_work_finished_signal_.SignalAll();
    Log(options_.leveling_compaction_info_log, "Finished leveling CompactMemTable\n\n");
  }

  if (hot_imm_ != nullptr) {
    Log(options_.info_log, "Starting tiering CompactHotMemTable");
    CompactTieringMemTable();
    background_work_finished_signal_.SignalAll();
    Log(options_.info_log, "Finished tiering CompactHotMemTable\n\n");
  }

  if(!versions_->NeedsCompaction()){
    return ;
  }

  
  Compaction* c;
  std::vector<Compaction*> Partitionleveling_compactions;
  TieringCompaction* tiering_com;
  bool is_manual = (manual_compaction_ != nullptr);
  InternalKey manual_end;

  // Log pointer address
  Log(options_.info_log, "BackgroundCompaction: Address of tiering_com pointer: %p", (void*)&tiering_com);

  if (is_manual) {
    ManualCompaction* m = manual_compaction_;
    c = versions_->CompactRange(m->level, m->begin, m->end);
    m->done = (c == nullptr);
    if (c != nullptr) {
      manual_end = c->input(0, c->num_input_files(0) - 1)->largest;
    }
    Log(options_.info_log,
        "Manual compaction at level-%d from %s .. %s; will stop at %s\n",
        m->level, (m->begin ? m->begin->DebugString().c_str() : "(begin)"),
        (m->end ? m->end->DebugString().c_str() : "(end)"),
        (m->done ? "(end)" : manual_end.DebugString().c_str()));
    
    // newly added source codes
    level_stats_[m->level].number_manual_compaction++;

  } else {
    versions_->PickCompaction(Partitionleveling_compactions, &tiering_com);
  }
  // Log pointer address and stored address after PickCompaction
  Log(options_.info_log, "After PickCompaction: Address stored in tiering_com: %p", (void*)tiering_com);
  // if(Partitionleveling_compactions.size() !=0){
  //   fprintf(stdout,"This time we have %lu partitions needs to be merged, start from %lu!\n",
  //     Partitionleveling_compactions.size(),Partitionleveling_compactions[0]->partition_num());
  // }
  

  Status status;
  if (tiering_com == nullptr && Partitionleveling_compactions.empty()) {
    // Nothing to do
  } else{
    for (Compaction* c : Partitionleveling_compactions) {
      uint64_t need_partition_num = c->partition_num();
      mem_partition_guard* temp_partition = nullptr;
      for(auto it = mem_partitions_.begin(); it != mem_partitions_.end(); ++it){
        if((*it)->partition_num == need_partition_num){
          temp_partition = *it;
          break;
        }
      }
      if(c->level() == 0){
        auto map_it = partition_first_L0flush_map_.find(need_partition_num);
        if (map_it == partition_first_L0flush_map_.end()) {
          fprintf(stderr,"current merged partition is:%lu, we need to continue\n", need_partition_num);
          delete c;
          continue;  
        }
      }
      bool is_first_L0flush =  partition_first_L0flush_map_[need_partition_num];
      if (is_first_L0flush && c->level()==0 && temp_partition->GetAverageFileSize()<1024){
        std::vector<uint64_t> deleted_partition_nums;
        Merge_all_supersmall_partitions(deleted_partition_nums);
        versions_->RePickCompaction(c, deleted_partition_nums);
        CompactionState* compact = new CompactionState(c);
        fprintf(stderr,"we need do a special compaction for L0\n");
        status = DoCompactionWork(compact, true);
            
        if (!status.ok()) {
          RecordBackgroundError(status);
        }

        CleanupCompaction(compact);
        c->ReleaseInputs();
        RemoveObsoleteFiles();
        fprintf(stderr,"we have finished a special compaction for L0\n");
      }else if (!is_manual && c->IsTrivialMove()) { 
        // Move file to next level
        assert(c->num_input_files(0) == 1);
        FileMetaData* f = c->input(0, 0);
        c->edit()->RemovePartitionFile(c->level(), c->partition_num(), f->number);
        c->edit()->AddPartitionLevelingFile(c->partition_num(), c->level() + 1, f->number, 
                f->file_size, f->smallest,f->largest);
        status = versions_->LogAndApply(c->edit(), &mutex_);
        if (!status.ok()) {
          RecordBackgroundError(status);
        }
        VersionSet::LevelSummaryStorage tmp;
        Log(options_.leveling_compaction_info_log, "Moved #%lld to level-%d %lld bytes %s: %s\n",
            static_cast<unsigned long long>(f->number), c->level() + 1,
            static_cast<unsigned long long>(f->file_size),
            status.ToString().c_str(), versions_->LevelSummary(&tmp));

        // newly added source codes
        level_stats_[c->level()+1].moved_directly_from_last_level_bytes = f->file_size;
        level_stats_[c->level()].moved_from_this_level_bytes = f->file_size;
        level_stats_[c->level()].number_TrivialMove++;
      } else {

        CompactionState* compact = new CompactionState(c);
        
        if(c->level()==0 && !is_first_L0flush){
          status = DoL0CompactionWork(compact);
        }else if(c->level()==0 && is_first_L0flush){
          status = DoCompactionWork(compact);
        }else if(c->level()>0){
          status = DoCompactionWorkWithoutIdentification(compact);
        }
            
        if (!status.ok()) {
          RecordBackgroundError(status);
        }

        CleanupCompaction(compact);
        c->ReleaseInputs();
        RemoveObsoleteFiles();
      }
      Log(options_.leveling_compaction_info_log, "\n\n");
      delete c;
    }

    if(tiering_com != nullptr){
      if (!is_manual && tiering_com->IsTrivialMoveWithTier()) { 
        // Move file to next level
        assert(tiering_com->num_input_tier_files(0) == 1);
        FileMetaData* f = tiering_com->tier_input(0, 0);
        tiering_com->edit()->RemovetieringFile(tiering_com->level(),tiering_com->get_selected_run_in_input_level(), f->number);
        tiering_com->edit()->AddTieringFile(tiering_com->level() + 1, tiering_com->get_selected_run_in_next_level(), f->number, f->file_size, f->smallest,
                          f->largest);
        tiering_com->edit()->set_is_tiering();
        status = versions_->LogAndApply(tiering_com->edit(), &mutex_);
        if (!status.ok()) {
          RecordBackgroundError(status);
        }
        VersionSet::LevelSummaryStorage tmp;
        Log(options_.info_log, "Moved #%lld to level-%d %lld bytes %s: %s\n",
            static_cast<unsigned long long>(f->number), tiering_com->level() + 1,
            static_cast<unsigned long long>(f->file_size),
            status.ToString().c_str(), versions_->LevelSummary(&tmp));

        // newly added source codes
        level_stats_[tiering_com->level()+1].moved_directly_from_last_level_bytes = f->file_size;
        level_stats_[tiering_com->level()].moved_from_this_level_bytes = f->file_size;
        level_stats_[tiering_com->level()].number_TrivialMove++;
      
      }else{
        TieringCompactionState* tier_compact = new TieringCompactionState(tiering_com);
        status = DoTieringCompactionWork(tier_compact);
        if (!status.ok()) {
          RecordBackgroundError(status);
        }
        CleanupCompaction(tier_compact);
        tiering_com->ReleaseInputs();
        TieringRemoveObsoleteFiles();
      }
      Log(options_.info_log, "\n\n");
      delete tiering_com; 
      tiering_com = nullptr;  
    }
  } 

  if (status.ok()) {
    // Done
  } else if (shutting_down_.load(std::memory_order_acquire)) {
    // Ignore compaction errors found during shutting down
  } else {
    Log(options_.info_log, "Compaction error: %s", status.ToString().c_str());
  }

  if (is_manual) {
    ManualCompaction* m = manual_compaction_;
    if (!status.ok()) {
      m->done = true;
    }
    if (!m->done) {
      // We only compacted part of the requested range.  Update *m
      // to the range that is left to be compacted.
      m->tmp_storage = manual_end;
      m->begin = &m->tmp_storage;
    }
    manual_compaction_ = nullptr;
  }
}

void DBImpl::CleanupCompaction(CompactionState* compact) {
  mutex_.AssertHeld();
  if (compact->builder != nullptr) {
    // May happen if we get a shutdown call in the middle of compaction
    compact->builder->Abandon();
    delete compact->builder;
  } else {
    assert(compact->outfile == nullptr);
  }
  delete compact->outfile;
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    pending_outputs_.erase(out.number);
  }
  delete compact;
}

void DBImpl::CleanupCompaction(TieringCompactionState* compact) {
  mutex_.AssertHeld();
  if (compact->builder != nullptr) {
    // May happen if we get a shutdown call in the middle of compaction
    compact->builder->Abandon();
    delete compact->builder;
  } else {
    assert(compact->outfile == nullptr);
  }
  delete compact->outfile;
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const TieringCompactionState::Output& out = compact->outputs[i];
    pending_outputs_.erase(out.number);
  }
  delete compact;
}

Status DBImpl::OpenCompactionOutputFile(CompactionState* compact, bool is_hot) {
  assert(compact != nullptr);
  if(!is_hot){
    assert(compact->builder == nullptr);
  }else{
    assert(compact->hot_builder == nullptr);
  }
  
  uint64_t file_number;

  {
    mutex_.Lock();
    file_number = versions_->NewFileNumber();
    pending_outputs_.insert(file_number);
    CompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    if(is_hot){
      compact->hot_outputs.push_back(out);
      // fprintf(stderr,"we have added a new hot output file\n");
    }else{
      compact->outputs.push_back(out);
      // fprintf(stderr,"we have added a new output file\n");
      // exit(0);
    }
    mutex_.Unlock();
  }

  // Make the output file
  Status s;
  if(!is_hot){
    std::string fname = TableFileName(dbname_, file_number);
    s = env_->NewWritableFile(fname, &compact->outfile);
    if (s.ok()) {
      compact->builder = new TableBuilder(options_, compact->outfile);
    }
  }else{
    std::string fname = TableFileName(dbname_, file_number);
    s = env_->NewWritableFile(fname, &compact->hot_outfile);
    if (s.ok()) {
      compact->hot_builder = new TableBuilder(options_, compact->hot_outfile);
    }
  }
  
  return s;
}


Status DBImpl::OpenHotCompactionOutputFile(CompactionState* compact) {
  assert(compact != nullptr);
  assert(compact->hot_builder == nullptr);
  uint64_t file_number;

  {
    mutex_.Lock();
    file_number = versions_->NewFileNumber();
    pending_outputs_.insert(file_number);
    CompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    compact->outputs.push_back(out);
    mutex_.Unlock();
  }

  // Make the output file
  std::string fname = TableFileName(dbname_, file_number);
  Status s = env_->NewWritableFile(fname, &compact->hot_outfile);
  if (s.ok()) {
    compact->hot_builder = new TableBuilder(options_, compact->hot_outfile);
  }
  return s;
}

Status DBImpl::OpenCompactionOutputFile(TieringCompactionState* compact) {
  assert(compact != nullptr);
  assert(compact->builder == nullptr);
  uint64_t file_number;
  {
    mutex_.Lock();
    file_number = versions_->NewFileNumber();
    pending_outputs_.insert(file_number);
    TieringCompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    compact->outputs.push_back(out);
    mutex_.Unlock();
  }

  // Make the output file
  std::string fname = TableFileName(dbname_, file_number);
  Status s = env_->NewWritableFile(fname, &compact->outfile);
  if (s.ok()) {
    compact->builder = new TableBuilder(options_, compact->outfile);
  }
  return s;
}

Status DBImpl::FinishCompactionOutputFile(CompactionState* compact,
                                          Iterator* input, bool is_hot) {
  assert(compact != nullptr);
  if(!is_hot){
    assert(compact->outfile != nullptr);
    assert(compact->builder != nullptr);
  }else{
    assert(compact->hot_outfile != nullptr);
    assert(compact->hot_builder != nullptr);
  }
  
  Status s;
  
  if(!is_hot){
    const uint64_t output_number = compact->current_output()->number;
    assert(output_number != 0);
    // Check for iterator errors
    Status s = input->status();
    const uint64_t current_entries = compact->builder->NumEntries();
    if (s.ok()) {
      s = compact->builder->Finish();
    } else {
      compact->builder->Abandon();
    }
    const uint64_t current_bytes = compact->builder->FileSize();
    compact->current_output()->file_size = current_bytes;
    compact->total_bytes += current_bytes;
    delete compact->builder;
    compact->builder = nullptr;

    // Finish and check for file errors
    if (s.ok()) {
      s = compact->outfile->Sync();
    }
    if (s.ok()) {
      s = compact->outfile->Close();
    }
    delete compact->outfile;
    compact->outfile = nullptr;

    if (s.ok() && current_entries > 0) {
      // Verify that the table is usable
      Iterator* iter =
          table_cache_->NewIterator(ReadOptions(), output_number, current_bytes);
      s = iter->status();
      delete iter;
      if (s.ok()) {
        Log(options_.leveling_compaction_info_log, "Generated table start:[%s] end:[%s] #%llu@%d: %lld keys, %lld bytes",
          compact->current_output()->smallest.user_key().ToString().c_str(), compact->current_output()->largest.user_key().ToString().c_str(),
          (unsigned long long)output_number, compact->compaction->level(),(unsigned long long)current_entries,
          (unsigned long long)current_bytes);
      }
    }
  }else{
    const uint64_t output_number = compact->current_hot_output()->number;
    assert(output_number != 0);
    // Check for iterator errors
    Status s = input->status();
    const uint64_t current_entries = compact->hot_builder->NumEntries();
    if (s.ok()) {
      s = compact->hot_builder->Finish();
    } else {
      compact->hot_builder->Abandon();
    }
    const uint64_t current_bytes = compact->hot_builder->FileSize();
    compact->current_hot_output()->file_size = current_bytes;
    compact->total_bytes += current_bytes;
    delete compact->hot_builder;
    compact->hot_builder = nullptr;

    // Finish and check for file errors
    if (s.ok()) {
      s = compact->hot_outfile->Sync();
    }
    if (s.ok()) {
      s = compact->hot_outfile->Close();
    }
    delete compact->hot_outfile;
    compact->hot_outfile = nullptr;

    if (s.ok() && current_entries > 0) {
      // Verify that the table is usable
      Iterator* iter =
          table_cache_->NewIterator(ReadOptions(), output_number, current_bytes);
      s = iter->status();
      delete iter;
      if (s.ok()) {
        Log(options_.leveling_compaction_info_log, "Generated a hot table start:[%s] end:[%s] #%llu@%d: %lld keys, %lld bytes",
          compact->current_hot_output()->smallest.user_key().ToString().c_str(), compact->current_hot_output()->largest.user_key().ToString().c_str(),
          (unsigned long long)output_number, compact->compaction->level(),(unsigned long long)current_entries,
          (unsigned long long)current_bytes);
      }
    }
  }
  return s;
}

Status DBImpl::FinishCompactionOutputFile(TieringCompactionState* compact,
                                          Iterator* input) {
  assert(compact != nullptr);
  assert(compact->outfile != nullptr);
  assert(compact->builder != nullptr);

  const uint64_t output_number = compact->current_output()->number;
  assert(output_number != 0);

  // Check for iterator errors
  Status s = input->status();
  const uint64_t current_entries = compact->builder->NumEntries();
  if (s.ok()) {
    s = compact->builder->Finish();
  } else {
    compact->builder->Abandon();
  }
  const uint64_t current_bytes = compact->builder->FileSize();
  compact->current_output()->file_size = current_bytes;
  compact->total_bytes += current_bytes;
  delete compact->builder;
  compact->builder = nullptr;

  // Finish and check for file errors
  if (s.ok()) {
    s = compact->outfile->Sync();
  }
  if (s.ok()) {
    s = compact->outfile->Close();
  }
  delete compact->outfile;
  compact->outfile = nullptr;

  if (s.ok() && current_entries > 0) {
    // Verify that the table is usable
    Iterator* iter =
        table_cache_->NewIterator(ReadOptions(), output_number, current_bytes);
    s = iter->status();
    delete iter;
    if (s.ok()) {
      Log(options_.info_log, "Generated table #%llu@%d: %lld keys, %lld bytes",
          (unsigned long long)output_number, compact->tiercompaction->level(),
          (unsigned long long)current_entries,
          (unsigned long long)current_bytes);
    }
  }
  return s;
}

Status DBImpl::CreateL1PartitionAndInstallCompactionResults(CompactionState* compact) {
  mutex_.AssertHeld();

  Log(options_.leveling_compaction_info_log, "Compacted %d@%d + %d@%d files => %lld bytes",
    compact->compaction->num_input_files(0), compact->compaction->level(),
    compact->compaction->num_input_files(1), compact->compaction->level() + 1,
    static_cast<long long>(compact->total_bytes));

  const uint64_t com_partition_num = compact->compaction->partition_num();
  const int level = compact->compaction->level();
  compact->compaction->AddPartitionInputDeletions(com_partition_num, compact->compaction->edit());

  if(!compact->outputs.empty()){
    mem_partition_guard* parent_partition = nullptr;
    for(auto it = mem_partitions_.begin(); it != mem_partitions_.end(); ++it){
      if((*it)->partition_num == com_partition_num){
        parent_partition = *it;
        break;
      }
    }

    size_t i = 0;
    for(; i < compact->outputs.size(); i++){
      const CompactionState::Output& out = compact->outputs[i];
      // Log the smallest and largest keys for each file
      Log(options_.leveling_compaction_info_log, "File #%llu: smallest=%s, largest=%s",
        (unsigned long long)out.number,
        out.smallest.user_key().ToString().c_str(),
        out.largest.user_key().ToString().c_str());
      
      std::string new_partition_start = out.smallest.user_key().ToString();
      std::string new_partition_end = out.largest.user_key().ToString();
      mem_partition_guard* new_partition = new mem_partition_guard(new_partition_start, new_partition_end);
      new_partition->partition_num = versions_->NewPartitionNumber();
      parent_partition->sub_partitions_.insert(new_partition);

      // Log the creation of the new partition and its insertion into the parent partition
      Log(options_.leveling_compaction_info_log, "Created new partition %lu with range [%s, %s] and inserted into parent partition %lu",
        new_partition->partition_num,new_partition_start.c_str(), new_partition_end.c_str(), parent_partition->partition_num);

      // Add compaction outputs
      compact->compaction->edit()->AddPartitionLevelingFile(new_partition->partition_num, level + 1, out.number, out.file_size,
                                          out.smallest, out.largest);

    }
    auto it = parent_partition->sub_partitions_.end();
    it--;
    (*it)->UpdatePartitionEnd(parent_partition->partition_end.ToString());

    // set
    partition_first_L0flush_map_[com_partition_num] = false;
  }
  
  if(!compact->hot_outputs.empty()){

  }

  return versions_->LogAndApply(compact->compaction->edit(), &mutex_);
}

Status DBImpl::InstallCompactionResults(CompactionState* compact) {
  mutex_.AssertHeld();
  Log(options_.leveling_compaction_info_log, "Compacted %d@%d + %d@%d files => %lld bytes",
    compact->compaction->num_input_files(0), compact->compaction->level(),
    compact->compaction->num_input_files(1), compact->compaction->level() + 1,
    static_cast<long long>(compact->total_bytes));

  // Add compaction outputs
  const uint64_t partition_num = compact->compaction->partition_num();
  const int level = compact->compaction->level();

  if(compact->compaction->is_merge_compaction()){
    compact->compaction->AddL0MergePartitionInputDeletions(compact->compaction->edit());
  }else{
    compact->compaction->AddPartitionInputDeletions(partition_num, compact->compaction->edit());
  }

  if(compact->compaction->level() == 0 && !compact->compaction->is_merge_compaction()){
    assert(compact->L1_partitions_.size() == compact->outputs.size());
    size_t i = 0;
    for (; i < compact->outputs.size(); i++) {
      const CompactionState::Output& out = compact->outputs[i];
      compact->compaction->edit()->AddPartitionLevelingFile(compact->L1_partitions_[i], level + 1, out.number, out.file_size,
                                        out.smallest, out.largest);
      Log(options_.leveling_compaction_info_log, "finished a compaction output file:Partition[%lu] File #%llu: size=%llu, smallest=%s, largest=%s",
        compact->L1_partitions_[i], (unsigned long long)out.number,(unsigned long long)out.file_size,
        out.smallest.user_key().ToString().c_str(), out.largest.user_key().ToString().c_str());
    }
    // if(current_sub_partition->CompareWithEnd(compact->outputs[i].largest.user_key().data(), compact->outputs[i].largest.user_key().size())<0 ){

    // }
  }else{
    for (size_t i = 0; i < compact->outputs.size(); i++) {
      const CompactionState::Output& out = compact->outputs[i];
      compact->compaction->edit()->AddPartitionLevelingFile(partition_num, level + 1, out.number, out.file_size,
                                        out.smallest, out.largest);
    }
  }
  return versions_->LogAndApply(compact->compaction->edit(), &mutex_);
}


Status DBImpl::InstallTieringCompactionResults(TieringCompactionState* compact) {
  mutex_.AssertHeld();
  Log(options_.info_log, "Compacted %d@%d + %d@%d tiering files => %lld bytes",
      compact->tiercompaction->num_input_tier_files(0), compact->tiercompaction->level(),
      compact->tiercompaction->num_input_tier_files(1), compact->tiercompaction->level() + 1,
      static_cast<long long>(compact->total_bytes));

  // Add compaction outputs
  compact->tiercompaction->AddTieringInputDeletions(compact->tiercompaction->edit());
  const int level = compact->tiercompaction->level();
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const TieringCompactionState::Output& out = compact->outputs[i];
    compact->tiercompaction->edit()->AddTieringFile(level + 1, compact->tiercompaction->get_selected_run_in_next_level(), out.number, out.file_size,
                                         out.smallest, out.largest);
  }
  compact->tiercompaction->edit()->set_is_tiering();
  return versions_->LogAndApply(compact->tiercompaction->edit(), &mutex_);
}


void DBImpl::loadKeysFromCSV(const std::string& filePath){
    std::ifstream file(filePath);
    std::string line;
    // Skip the title line
    std::getline(file, line);

    size_t keysCount = 0; // 用于计数读取了多少个key

    while (std::getline(file, line)) {
      std::stringstream ss(line);
      std::string keyStr, sizeStr;

      uint64_t key;
      size_t size;

      std::getline(ss, keyStr, ',');
      std::getline(ss, sizeStr, ',');
      std::getline(ss, line, ',');

      key = std::stoll(keyStr);
      size = std::stoull(sizeStr);

      char kye_string[1024];
      char format[20];
      std::snprintf(format, sizeof(format), "%%0%zullu", size);
      std::snprintf(kye_string, sizeof(kye_string), format, (unsigned long long)key);
      // std::string formattedKey(size+1, '\0');
      // std::snprintf(&formattedKey[0], size + 1, format, (unsigned long long)key);
      // keyStorage.push_back(formattedKey);
      
      Slice sliceKey(kye_string,size);
      specialKeys.insert(sliceKey);

      std::string str(sliceKey.data(), sliceKey.size());
      uint64_t value = std::stoull(str);
      std::fprintf(stdout, "%ld\n", value);

      keysCount++; // 增加读取的key数量
  }

  fprintf(stderr, "Loaded %zu hot keys from %s.\n", keysCount, filePath.c_str());
}

void DBImpl::load_keys_from_CSV(const std::string& filePath) {
    std::ifstream file(filePath);
    std::string line;
    std::getline(file, line); // 跳过标题行

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string keyStr;
        uint64_t key;

        std::getline(ss, keyStr, ',');
        key = std::stoull(keyStr);

        hot_keys.insert(key);
    }

    fprintf(stderr, "Loaded %zu hot keys from %s \n", specialKeys.size(), filePath.c_str());
}


void DBImpl::batch_load_keys_from_CSV(const std::string& filePaths, const std::string& percentagesStr){
  
  std::vector<std::string> files;
  std::vector<int> percentages;
  std::stringstream ssFiles(filePaths);
  std::stringstream ssPercentages(percentagesStr);
  std::string item;

  // 解析hot files路径
  while (std::getline(ssFiles, item, ',')) {
    files.push_back(item);
    fprintf(stderr, "Parsed file: %s\n", item.c_str());
  }

  // 解析percentages
  while (std::getline(ssPercentages, item, ',')) {
    percentages.push_back(std::stoi(item));
    fprintf(stdout, "Parsed percentage: %d\n", std::stoi(item));
  }


  if (files.size() != percentages.size()) {
    fprintf(stderr,"Error: The number of files does not match the number of percentages.\n");
    return;
  }

  for (size_t i = 0; i < files.size(); ++i) {
    std::unordered_set<uint64_t> current_set;
    std::ifstream file(files[i]);
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
      std::stringstream lineStream(line);
      std::string keyStr;
      uint64_t key;

      std::getline(lineStream, keyStr, ',');
      key = std::stoll(keyStr);
      current_set.insert(key);
    }

    fprintf(stderr, "Loaded %zu hot keys from %s for %d%% \n", current_set.size(), files[i].c_str(), percentages[i]);
    hot_keys_sets[percentages[i]] = current_set;
  }
}

void DBImpl::testSpecialKeys() {

    const std::string& testFilePath = "/home/jeff-wang/workloads/etc_output_file1.02.csv";
    std::ifstream file(testFilePath);
    std::string line;

    std::getline(file, line);

    size_t totalKeysTested = 0;
    size_t specialKeysCount = 0;

    fprintf(stderr, "Testing special keys from file: %s\n", testFilePath.c_str());

    // uint64_t key = 1;
    // char keyString[1024];
    // std::snprintf(keyString, sizeof(keyString), "%016llu", (unsigned long long)key);
    // Slice sliceKey(keyString, std::strlen(keyString));

    // uint64_t key2 = 1 ;
    // size_t size = 15;
    // char kye_string[1024];
    // char format[20];
    // std::snprintf(format, sizeof(format), "%%0%zullu", size);
    // std::snprintf(kye_string, sizeof(kye_string), format, (unsigned long long)key);
    // std::string formattedKey(size+1, '\0');
    // std::snprintf(&formattedKey[0], size + 1, format, (unsigned long long)key2);
    // Slice sliceKey2(kye_string, formattedKey.size());
    // Slice sliceKey2(kye_string, size);

    // if(user_comparator()->Compare(sliceKey, sliceKey2) == 0){
    //     fprintf(stdout, "Key %ld is special in comparator.\n", key2);
    // } else {
    //     fprintf(stdout, "Key %ld is not special in comparator.\n", key2);
    // }

    // if(sliceKey.compare(sliceKey2) == 0){
    //     fprintf(stdout, "Key %ld is special in slice.\n", key2);
    // } else {
    //     fprintf(stdout, "Key %ld is not special in slice.\n", key2);
    // }

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string keyStr;

        uint64_t key;
        size_t size = 1024; 

        std::getline(ss, keyStr, ',');
        std::getline(ss, line, ',');
        
        key = std::stoll(keyStr);
        char keyString[1024];
        std::snprintf(keyString, sizeof(keyString), "%054llu", (unsigned long long)key);

        if (isSpecialKey(Slice(keyString, std::strlen(keyString)))) {
            fprintf(stdout, "Key %ld is special.\n", key); 
        } else {
            fprintf(stdout, "Key %ld is not special.\n", key);
        }
        totalKeysTested++;
        fflush(stdout);
        // exit(0);
    }
    fprintf(stderr, "Test complete. %zu out of %zu keys tested are special.\n", specialKeysCount, totalKeysTested);
    fflush(stderr);
    
}


void DBImpl::test_hot_keys() {
  const std::string& testFilePath = "/home/jeff-wang/workloads/etc_output_file1.02.csv";
    std::ifstream file(testFilePath);
    std::string line;
    std::getline(file, line); 

    size_t totalKeysTested = 0;
    size_t specialKeysCount = 0;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string keyStr;
        uint64_t key;

        std::getline(ss, keyStr, ',');
        key = std::stoll(keyStr);

        if (is_hot_key(key)) {
            // fprintf(stdout, "Key %ld is special.\n", key);
            specialKeysCount++;
        } else {
            fprintf(stdout, "Key %ld is not hot.\n", key);
        }
        totalKeysTested++;
    }
    fprintf(stderr, "Tested %zu keys, %zu are hot.\n", totalKeysTested, specialKeysCount);
}



std::vector<int> DBImpl::GetLevelPercents() {
    std::vector<int> percents;
    for (const auto& percentage_set : hot_keys_sets) {
        percents.push_back(percentage_set.first);
    }
    return percents;
  }


void  DBImpl::initialize_level_hotcoldstats(){
  std::vector<int> percents = GetLevelPercents(); // 获取所有可能的百分比定义
  level_hot_cold_stats.resize(config::kNumLevels); // 根据level数量初始化向量大小

    for (unsigned i = 0; i < level_hot_cold_stats.size(); ++i) 
    {
      for (size_t j = 0; j < percents.size(); ++j) 
      {
        level_hot_cold_stats[i].insert(std::make_pair(percents[j], LevelHotColdStats())) ; // 为每个百分比初始化LevelHotColdStats对象
      }
    }
    fprintf(stderr,"initialize %zu objects(LevelHotColdStats) within these %d levels\n", percents.size(),config::kNumLevels );
}

Status DBImpl::DoTieringCompactionWork(TieringCompactionState* compact) {
  const uint64_t start_micros = env_->NowMicros();
  int64_t imm_micros = 0;  // Micros spent doing imm_ compactions

  new_LeveldataStats new_compact_statistics;

  // compact->compaction->num_input_files(), 0代表要发生合并的level的文件的数量，1代表有overlap的level的文件的数量
  // compact->compaction->num_input_files(1) 示与当前级别有重叠的下一个级别（level + 1）中参与压缩的文件数量。
  Log(options_.info_log, "Compacting %d@%d + %d@%d files",
      compact->tiercompaction->num_input_tier_files(0), compact->tiercompaction->level(),
      compact->tiercompaction->num_input_tier_files(1),
      compact->tiercompaction->level() + 1);

  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // record the number of files that involved in every compaction!
  
  if(compact->tiercompaction->get_compaction_type() == 1){ // size compaction
    level_stats_[compact->tiercompaction->level()].number_size_tieirng_compactions++;
    level_stats_[compact->tiercompaction->level()].number_size_compaction_tieirng_initiator_files += compact->tiercompaction->num_input_tier_files(0);
    level_stats_[compact->tiercompaction->level()+1].number_size_compaction_tieirng_participant_files += compact->tiercompaction->num_input_tier_files(1);
  }   
  else if(compact->tiercompaction->get_compaction_type() == 2){ // seek compaction 
    level_stats_[compact->tiercompaction->level()].number_seek_tiering_compactions++;
    level_stats_[compact->tiercompaction->level()].number_seek_tiering_compaction_initiator_files += compact->tiercompaction->num_input_tier_files(0);
    level_stats_[compact->tiercompaction->level()+1].number_seek_tiering_compaction_participant_files += compact->tiercompaction->num_input_tier_files(1);
  }
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  // 这个 compact->compaction->level() 是指当前 compaction 的 level，也是就是哪个level需要被合并
  assert(versions_->Num_Level_tiering_Files(compact->tiercompaction->level()) > 0);
  assert(compact->builder == nullptr);
  assert(compact->outfile == nullptr);
  if (snapshots_.empty()) {
    compact->smallest_snapshot = versions_->LastSequence();
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
  }

  // 制作一个迭代器
  Iterator* input = versions_->MakeTieringInputIterator(compact->tiercompaction);

  // Release mutex while we're actually doing the compaction work
  mutex_.Unlock();

  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;

  while (input->Valid() && !shutting_down_.load(std::memory_order_acquire)) {
    // Prioritize immutable compaction work
    if (has_hot_imm_.load(std::memory_order_relaxed)) {
      const uint64_t imm_start = env_->NowMicros();
      mutex_.Lock();
      if (hot_imm_ != nullptr) {
        CompactTieringMemTable();
        // Wake up MakeRoomForWrite() if necessary.
        background_work_finished_signal_.SignalAll();
      }
      mutex_.Unlock();
      imm_micros += (env_->NowMicros() - imm_start);
    }

    Slice key = input->key();

    // if (compact->compaction->ShouldStopBefore(key) &&
    //     compact->builder != nullptr) {
    //   status = FinishCompactionOutputFile(compact, input);
    //   if (!status.ok()) {
    //     break;
    //   }
    // }

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
              0) {
        // First occurrence of this user key
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by an newer entry for same user key
        drop = true;  // (A)
      } else if (ikey.type == kTypeDeletion &&
                 ikey.sequence <= compact->smallest_snapshot &&
                 compact->tiercompaction->IsBaseLevelForKey(ikey.user_key)) {
        // For this user key:
        // (1) there is no data in higher levels
        // (2) data in lower levels will have larger sequence numbers
        // (3) data in layers that are being compacted here and have
        //     smaller sequence numbers will be dropped in the next
        //     few iterations of this loop (by rule (A) above).
        // Therefore this deletion marker is obsolete and can be dropped.
        drop = true;
      }
      last_sequence_for_key = ikey.sequence;
    }
    
#if 0
    Log(options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeValue, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif

    if (!drop) {
      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          Log(options_.info_log, "Failed to open new compaction output file: %s", status.ToString().c_str());
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        Log(options_.info_log, "First entry in the new output file, key");
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());

      // Close output file if it is big enough
      if (compact->builder->FileSize() >=
          compact->tiercompaction->MaxOutputFileSize()) {
        Log(options_.info_log, "Output file size reached max limit, closing the file");
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          Log(options_.info_log, "Failed to finish compaction output file: %s", status.ToString().c_str());
          break;
        }
      }
    }

    input->Next();
  }

  if (status.ok() && shutting_down_.load(std::memory_order_acquire)) {
    status = Status::IOError("Deleting DB during compaction");
  }
  if (status.ok() && compact->builder != nullptr) {
    // Log(options_.info_log, "Finished all compaction output files, total number of output files: %lu", compact->outputs.size());
    status = FinishCompactionOutputFile(compact, input);
  }
  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;

  CompactionStats stats;
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  uint64_t init_level_bytes_read = 0;
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

  stats.micros = env_->NowMicros() - start_micros - imm_micros;
  for (int which = 0; which < 2; which++) {
    for (int i = 0; i < compact->tiercompaction->num_input_tier_files(which); i++) {
      stats.bytes_read += compact->tiercompaction->tier_input(which, i)->file_size;
    }
  }

  for (size_t i = 0; i < compact->outputs.size(); i++) {
    stats.bytes_written += compact->outputs[i].file_size;
  }

  mutex_.Lock();
  stats_[compact->tiercompaction->level() + 1].Add(stats);
  level_stats_[compact->tiercompaction->level() + 1].tiering_bytes_read += stats.bytes_read;
  level_stats_[compact->tiercompaction->level() + 1].tiering_bytes_written += stats.bytes_written;

  if (status.ok()) {
    status = InstallTieringCompactionResults(compact);
  }
  if (!status.ok()) {
    RecordBackgroundError(status);
  }
  VersionSet::LevelSummaryStorage tmp;
  Log(options_.info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
  return status;
}


Status DBImpl::DoCompactionWorkWithoutIdentification(CompactionState* compact) {
  const uint64_t start_micros = env_->NowMicros();
  int64_t imm_micros = 0;  // Micros spent doing imm_ compactions

  new_LeveldataStats new_compact_statistics;

  // compact->compaction->num_input_files(), 0代表要发生合并的level的文件的数量，1代表有overlap的level的文件的数量
  // compact->compaction->num_input_files(1) 示与当前级别有重叠的下一个级别（level + 1）中参与压缩的文件数量。
  Log(options_.leveling_compaction_info_log, "[Partition Leveling: Partition %lu]Compacting %d@%d + %d@%d files",
    compact->compaction->partition_num(), compact->compaction->num_input_files(0), compact->compaction->level(),
    compact->compaction->num_input_files(1), compact->compaction->level() + 1);

  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // record the number of files that involved in every compaction!
  if(compact->compaction->get_compaction_type() == 1){ // size compaction
  level_stats_[compact->compaction->level()].number_size_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_size_compaction_leveling_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_size_compaction_leveling_participant_files += compact->compaction->num_input_files(1);
  }
  else if(compact->compaction->get_compaction_type() == 2){ // seek compaction 
    level_stats_[compact->compaction->level()].number_seek_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_seek_leveling_compaction_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_seek_leveling_compaction_participant_files += compact->compaction->num_input_files(1);
  }
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  // 这个 compact->compaction->level() 是指当前 compaction 的 level，也是就是哪个level需要被合并
  if(!compact->compaction->is_merge_compaction()){
    assert(versions_->Num_Level_Partitionleveling_Files(compact->compaction->level(), compact->compaction->partition_num()) > 0);
  }

  assert(compact->builder == nullptr);
  assert(compact->outfile == nullptr);
  assert(compact->compaction->level() >=1 );

  if (snapshots_.empty()) {
    compact->smallest_snapshot = versions_->LastSequence();
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
  }

  // 制作一个迭代器
  Iterator* input = versions_->MakeInputIterator(compact->compaction);

  // Release mutex while we're actually doing the compaction work
  mutex_.Unlock();

  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  bool is_output = false;
  int i = 0;
  int sequence_of_each_key = 0;
  if(compact->compaction->level() ==1 ){
    is_output = true;
  }


  while (input->Valid() && !shutting_down_.load(std::memory_order_acquire)) {
    // Prioritize immutable compaction work
    if (has_imm_.load(std::memory_order_relaxed)) {
      const uint64_t imm_start = env_->NowMicros();
      mutex_.Lock();
      if (imm_ != nullptr) {
        CompactLevelingMemTable();
        // Wake up MakeRoomForWrite() if necessary.
        background_work_finished_signal_.SignalAll();
      }
      mutex_.Unlock();
      imm_micros += (env_->NowMicros() - imm_start);
    }

    Slice key = input->key();

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key || user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) != 0) {
        // Log(options_.leveling_compaction_info_log, "Last userkey occurs %d times, Parsed a new user key: %s, sequence: %llu, type: %d",
        //   sequence_of_each_key, ikey.user_key.ToString().c_str(), (unsigned long long)ikey.sequence, ikey.type);
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      } 

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by a newer entry for the same user key
        drop = true;  // (A)
        // Log(options_.leveling_compaction_info_log, "Key %s is hidden by a newer entry, last_sequence_for_key: %llu, smallest_snapshot: %llu",
        //     ikey.user_key.ToString().c_str(), (unsigned long long)last_sequence_for_key, (unsigned long long)compact->smallest_snapshot);
      } else if (ikey.type == kTypeDeletion &&
                ikey.sequence <= compact->smallest_snapshot &&
                compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
        // Deletion marker is obsolete and can be dropped
        drop = true;
        Log(options_.leveling_compaction_info_log, "Key %s is a deletion marker and can be dropped, sequence: %llu, smallest_snapshot: %llu",
            ikey.user_key.ToString().c_str(), (unsigned long long)ikey.sequence, (unsigned long long)compact->smallest_snapshot);
      } 

      last_sequence_for_key = ikey.sequence;
      // Log(options_.leveling_compaction_info_log, "Updated last_sequence_for_key: %llu\n\n", (unsigned long long)last_sequence_for_key);
    }
    
#if 0
    Log(options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeValue, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif
    
    if (!drop) {
      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());

      // Close output file if it is big enough
      if (compact->builder->FileSize() >=
          compact->compaction->MaxOutputFileSize()) {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          break;
        }
      }
    }
    input->Next();
    i++;
  }  

  Log(options_.leveling_compaction_info_log, "We have already executed %d operations!\n", i);
  if (status.ok() && shutting_down_.load(std::memory_order_acquire)) {
    status = Status::IOError("Deleting DB during compaction");
  }

  if (status.ok() && compact->builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input);
    if (!status.ok()) {
      Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error finishing last compaction output file: %s", status.ToString().c_str());
    }
  }

  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros - imm_micros;
  for (int which = 0; which < 2; which++) {

    for (int i = 0; i < compact->compaction->num_input_files(which); i++) {
      stats.bytes_read += compact->compaction->input(which, i)->file_size;
    }
  }

  for (size_t i = 0; i < compact->outputs.size(); i++) {
    stats.bytes_written += compact->outputs[i].file_size;
  }

  mutex_.Lock();

  stats_[compact->compaction->level() + 1].Add(stats);
  level_stats_[compact->compaction->level() + 1].leveling_bytes_read += stats.bytes_read;
  level_stats_[compact->compaction->level() + 1].leveling_bytes_written += stats.bytes_written;

  if (status.ok()) {
    status = InstallCompactionResults(compact);

    if(compact->hot_builder != nullptr){}
    
    if (!status.ok()) {
      Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error installing from L%d to L%d compaction results: %s",
        compact->compaction->level(), compact->compaction->level()+1, status.ToString().c_str());
    }
  } 
  if (!status.ok()) {
    RecordBackgroundError(status);
  }
  VersionSet::LevelSummaryStorage tmp;
  Log(options_.leveling_compaction_info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
  config::kInitialPartitionLevelingCompactionTrigger = 24;

  return status;
}

Status DBImpl::DoCompactionWork(CompactionState* compact, bool merge_small_ranges) {
  const uint64_t start_micros = env_->NowMicros();
  int64_t imm_micros = 0;  // Micros spent doing imm_ compactions

  new_LeveldataStats new_compact_statistics;

  // compact->compaction->num_input_files(), 0代表要发生合并的level的文件的数量，1代表有overlap的level的文件的数量
  // compact->compaction->num_input_files(1) 示与当前级别有重叠的下一个级别（level + 1）中参与压缩的文件数量。
  if(merge_small_ranges){
      Log(options_.leveling_compaction_info_log, "[small Partition:%s Leveling: Partition %lu]Compacting %d@%d + %d@%d files",
      compact->compaction->small_merge_partitions.c_str(), compact->compaction->partition_num(),
      compact->compaction->num_input_files(0), compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1);
  }else{
    Log(options_.leveling_compaction_info_log, "[Partition Leveling: Partition %lu]Compacting %d@%d + %d@%d files",
      compact->compaction->partition_num(),
      compact->compaction->num_input_files(0), compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1);
  }
  

  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // record the number of files that involved in every compaction!
  if(compact->compaction->get_compaction_type() == 1){ // size compaction
  level_stats_[compact->compaction->level()].number_size_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_size_compaction_leveling_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_size_compaction_leveling_participant_files += compact->compaction->num_input_files(1);
  }
  else if(compact->compaction->get_compaction_type() == 2){ // seek compaction 
    level_stats_[compact->compaction->level()].number_seek_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_seek_leveling_compaction_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_seek_leveling_compaction_participant_files += compact->compaction->num_input_files(1);
  }
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  // 这个 compact->compaction->level() 是指当前 compaction 的 level，也是就是哪个level需要被合并
  if(!compact->compaction->is_merge_compaction()){
    assert(versions_->Num_Level_Partitionleveling_Files(compact->compaction->level(), compact->compaction->partition_num()) > 0);
  }

  assert(compact->builder == nullptr);
  assert(compact->outfile == nullptr);
  if (snapshots_.empty()) {
    compact->smallest_snapshot = versions_->LastSequence();
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
  }

  // 制作一个迭代器
  Iterator* input = versions_->MakeInputIterator(compact->compaction);

  // Release mutex while we're actually doing the compaction work
  mutex_.Unlock();

  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  bool is_output = false;
  int i = 0;
  int sequence_of_each_key = 0;
  if(compact->compaction->level() ==1 ){
    is_output = true;
  }
  Slice last_user_key;
  std::string range_start, range_end;
  bool has_range = false;
  int user_key_numbers = 0;
  std::queue<std::string> key_queues;
 
  std::queue<std::string> key_queues_;
  std::queue<std::string> val_queues_;


  while (input->Valid() && !shutting_down_.load(std::memory_order_acquire)) {
    // Prioritize immutable compaction work
    if (has_imm_.load(std::memory_order_relaxed)) {
      const uint64_t imm_start = env_->NowMicros();
      mutex_.Lock();
      if (imm_ != nullptr) {
        CompactLevelingMemTable();
        // Wake up MakeRoomForWrite() if necessary.
        background_work_finished_signal_.SignalAll();
      }
      mutex_.Unlock();
      imm_micros += (env_->NowMicros() - imm_start);
    }

    Slice key = input->key();

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
        user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) != 0) {
        // Log(options_.leveling_compaction_info_log, "Last userkey occurs %d times, Parsed a new user key: %s, sequence: %llu, type: %d",
        //   sequence_of_each_key, ikey.user_key.ToString().c_str(), (unsigned long long)ikey.sequence, ikey.type);

        // First occurrence of this user key
        if (key_queues.size() == 3) {
          key_queues.pop();
        }
        key_queues.push(ikey.user_key.ToString());

        if(sequence_of_each_key==0){

        }else if(sequence_of_each_key > hot_key_threshold){
          if (compact->hot_builder == nullptr) {
            // fprintf(stderr, "Open hot output file in loop\n");
            status = OpenCompactionOutputFile(compact,true);
            if (!status.ok()) {
              Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error opening compaction output file: %s", status.ToString().c_str());
              break;
            }
          }
          while (!key_queues_.empty() && !val_queues_.empty()) {
            if (compact->hot_builder->NumEntries() == 0) {
              compact->current_hot_output()->smallest.DecodeFrom(key_queues_.front());
            }
            compact->current_hot_output()->largest.DecodeFrom(key_queues_.front());
            compact->hot_builder->Add(key_queues_.front(), val_queues_.front());
            // Log(options_.leveling_compaction_info_log, "Adding to hot_builder: key = %s", key_queues_.front().data());
            key_queues_.pop();
            val_queues_.pop();
          }
        }else if(sequence_of_each_key <= hot_key_threshold){ 
          // Open output file if necessary
          if (compact->builder == nullptr) {
            // fprintf(stderr, "Open output file in loop\n");
            status = OpenCompactionOutputFile(compact);
            if (!status.ok()) {
              Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error opening compaction output file: %s", status.ToString().c_str());
              break;
            }
          }
          while (!key_queues_.empty() && !val_queues_.empty()) {
            if (compact->builder->NumEntries() == 0) {
              compact->current_output()->smallest.DecodeFrom(key_queues_.front());
            }
            compact->current_output()->largest.DecodeFrom(key_queues_.front());
            compact->builder->Add(key_queues_.front(), val_queues_.front());
            // Log(options_.leveling_compaction_info_log, "Adding to builder: key = %s", key_queues_.front().data());
            key_queues_.pop();
            val_queues_.pop();
          }
          // Close output file if it is big enough
          if (compact->builder->FileSize() >=
              compact->compaction->MinOutputFileSize()) {
            status = FinishCompactionOutputFile(compact, input);
            if (!status.ok()) {
              Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error finishing compaction output file: %s", status.ToString().c_str());
              break;
            }
          }
        }

        if(sequence_of_each_key >hot_key_threshold && !has_range ){
          range_boundaries.push_back(current_user_key);
          has_range = true;
          // Log(options_.leveling_compaction_info_log, "Range start:[%s]", range_start.c_str());
        }else if(sequence_of_each_key <= hot_key_threshold && has_range){
          range_boundaries.push_back(key_queues.front());
          has_range = false;
          hot_range new_hot_range(range_boundaries[range_boundaries.size()-2].data(), range_boundaries[range_boundaries.size()-2].size(),
            range_boundaries[range_boundaries.size()-1].data(),range_boundaries[range_boundaries.size()-1].size());
          HotRanges->hot_ranges_.insert(new_hot_range);
          // Log(options_.leveling_compaction_info_log, "We created a new Range: [%s, %s]", new_hot_range.start_ptr, new_hot_range.end_ptr);
        }

        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;

        // Log(options_.leveling_compaction_info_log, "Last key occurs %ld times, New user key: %s",sequence_of_each_key, current_user_key.c_str());
        sequence_of_each_key = 1;
      } else {
        sequence_of_each_key++;
        // Log(options_.leveling_compaction_info_log, "Existing user key: %s", current_user_key.c_str());
      }

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by a newer entry for the same user key
        drop = true;  // (A)
        // Log(options_.leveling_compaction_info_log, "Key %s is hidden by a newer entry, last_sequence_for_key: %llu, smallest_snapshot: %llu",
        //     ikey.user_key.ToString().c_str(), (unsigned long long)last_sequence_for_key, (unsigned long long)compact->smallest_snapshot);
      } else if (ikey.type == kTypeDeletion &&
                ikey.sequence <= compact->smallest_snapshot &&
                compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
        // Deletion marker is obsolete and can be dropped
        drop = true;
        Log(options_.leveling_compaction_info_log, "Key %s is a deletion marker and can be dropped, sequence: %llu, smallest_snapshot: %llu",
            ikey.user_key.ToString().c_str(), (unsigned long long)ikey.sequence, (unsigned long long)compact->smallest_snapshot);
      } else {
        // Log(options_.leveling_compaction_info_log, "Key %s is kept, sequence: %llu, last_sequence_for_key: %llu",
        //     ikey.user_key.ToString().c_str(), (unsigned long long)ikey.sequence, (unsigned long long)last_sequence_for_key);
      }

      last_sequence_for_key = ikey.sequence;
      // Log(options_.leveling_compaction_info_log, "Updated last_sequence_for_key: %llu\n\n", (unsigned long long)last_sequence_for_key);
    }
    
#if 0
    Log(options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeValue, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif
    
    if (!drop) {
      key_queues_.push(key.ToString());
      val_queues_.push(input->value().ToString());
    }

    input->Next();
    i++;
  }

  if(sequence_of_each_key > hot_key_threshold){
    if (compact->hot_builder == nullptr) {
      // fprintf(stderr, "Open hot output file in ending\n");
      status = OpenCompactionOutputFile(compact,true);
      if (!status.ok()) {
        Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error opening compaction output file: %s", status.ToString().c_str());
      }
    }
    while (!key_queues_.empty() && !val_queues_.empty()) {
      if (compact->hot_builder->NumEntries() == 0) {
        compact->current_hot_output()->smallest.DecodeFrom(key_queues_.front());
      }
      compact->current_hot_output()->largest.DecodeFrom(key_queues_.front());
      compact->hot_builder->Add(key_queues_.front(), val_queues_.front());
      // Log(options_.leveling_compaction_info_log, "Adding to hot_builder: key = %s", key_queues_.front().data());
      key_queues_.pop();
      val_queues_.pop();
    }
  }else{
    if (compact->builder == nullptr) {
      // fprintf(stderr, "Open output file in ending\n");
      status = OpenCompactionOutputFile(compact);
      if (!status.ok()) {
        Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error opening compaction output file: %s", status.ToString().c_str());
      }
    }
    while (!key_queues_.empty() && !val_queues_.empty()) {
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key_queues_.front());
      }
      compact->current_output()->largest.DecodeFrom(key_queues_.front());
      compact->builder->Add(key_queues_.front(), val_queues_.front());
      // Log(options_.leveling_compaction_info_log, "Adding to hot_builder: key = %s", key_queues_.front().data());
      key_queues_.pop();
      val_queues_.pop();
    }
  }

  Log(options_.leveling_compaction_info_log, "We have already executed %d operations!\n", i);
  Log(options_.leveling_compaction_info_log, "Range size %lu", HotRanges->hot_ranges_.size());
  if (status.ok() && shutting_down_.load(std::memory_order_acquire)) {
    status = Status::IOError("Deleting DB during compaction");
  }

  if (status.ok() && compact->builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input);
    if (!status.ok()) {
      Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error finishing last compaction output file: %s", status.ToString().c_str());
    }
  }

  if (status.ok() && compact->hot_builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input, true);
    if (!status.ok()) {
      Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error finishing last compaction output file: %s", status.ToString().c_str());
    }
  }

  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros - imm_micros;
  for (int which = 0; which < 2; which++) {

    for (int i = 0; i < compact->compaction->num_input_files(which); i++) {
      stats.bytes_read += compact->compaction->input(which, i)->file_size;
    }
  }

  for (size_t i = 0; i < compact->outputs.size(); i++) {
    stats.bytes_written += compact->outputs[i].file_size;
  }

  mutex_.Lock();

  if (has_range && sequence_of_each_key > hot_key_threshold) {
    range_boundaries.push_back(key_queues.back());
    hot_range new_hot_range(range_boundaries[range_boundaries.size()-2].data(), range_boundaries[range_boundaries.size()-2].size(),
          range_boundaries[range_boundaries.size()-1].data(),range_boundaries[range_boundaries.size()-1].size());
    HotRanges->hot_ranges_.insert(new_hot_range);
    Log(options_.leveling_compaction_info_log, "Ending final hot range: %s - %s", new_hot_range.start_ptr, new_hot_range.end_ptr);
  }else if(!has_range && sequence_of_each_key > hot_key_threshold){
    range_boundaries.push_back(key_queues.back());
    range_boundaries.push_back(key_queues.back());
    hot_range new_hot_range(range_boundaries[range_boundaries.size()-2].data(), range_boundaries[range_boundaries.size()-2].size(),
      range_boundaries[range_boundaries.size()-1].data(),range_boundaries[range_boundaries.size()-1].size());
    HotRanges->hot_ranges_.insert(new_hot_range);
    Log(options_.leveling_compaction_info_log, "Ending final hot range: %s - %s", new_hot_range.start_ptr, new_hot_range.end_ptr);
  }

  if(!HotRanges->hot_ranges_.empty()){
    auto it = HotRanges->hot_ranges_.begin();
    auto it1 = HotRanges->hot_ranges_.end();
    it1--;
    
    HotRanges->min_max_range = new hot_range(it->start_ptr,it->start_size, it1->end_ptr,it1->end_size);
    // fprintf(stderr, "we create a min_max_range start: %.*s, end: %.*s\n",
    //     static_cast<int>(HotRanges->min_max_range->start_size),
    //     HotRanges->min_max_range->start_ptr,
    //     static_cast<int>(HotRanges->min_max_range->end_size),
    //     HotRanges->min_max_range->end_ptr);

    if (HotRanges->largest_intensity_range == nullptr) {
      HotRanges->largestindensity_start_str = it ->getStartString();
      HotRanges->largestindensity_end_str = it ->getEndString();
      // Create a new hot_range object if it is null
      HotRanges->largest_intensity_range = new hot_range(HotRanges->largestindensity_start_str.data(),HotRanges->largestindensity_start_str.size(),
                                               HotRanges->largestindensity_end_str.data(), HotRanges->largestindensity_end_str.size());
    }
  }

  stats_[compact->compaction->level() + 1].Add(stats);
  level_stats_[compact->compaction->level() + 1].leveling_bytes_read += stats.bytes_read;
  level_stats_[compact->compaction->level() + 1].leveling_bytes_written += stats.bytes_written;

  if (status.ok()) {
    if( compact->compaction->level()==0 && !compact->compaction->is_merge_compaction()){
      status = CreateL1PartitionAndInstallCompactionResults(compact);
    }else {
      status = InstallCompactionResults(compact);
    }

    if(compact->hot_builder != nullptr){}
    
    if (!status.ok()) {
      Log(options_.leveling_compaction_info_log, "DoCompactionWork: Error installing from L%d to L%d compaction results: %s",
        compact->compaction->level(), compact->compaction->level()+1, status.ToString().c_str());
    }
  } 
  if (!status.ok()) {
    RecordBackgroundError(status);
  }
  VersionSet::LevelSummaryStorage tmp;
  Log(options_.leveling_compaction_info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
  config::kInitialPartitionLevelingCompactionTrigger = 24;

  return status;
}

Status DBImpl::DoL0CompactionWork(CompactionState* compact) {
  const uint64_t start_micros = env_->NowMicros();
  int64_t imm_micros = 0;  // Micros spent doing imm_ compactions

  new_LeveldataStats new_compact_statistics;
  Log(options_.leveling_info_log, "[Partition Leveling: Partition %lu]Compacting %d@%d + %d@%d files",
      compact->compaction->partition_num(),
      compact->compaction->num_input_files(0), compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1);

  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // record the number of files that involved in every compaction!
  if(compact->compaction->get_compaction_type() == 1){ // size compaction
  level_stats_[compact->compaction->level()].number_size_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_size_compaction_leveling_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_size_compaction_leveling_participant_files += compact->compaction->num_input_files(1);
  }
  else if(compact->compaction->get_compaction_type() == 2){ // seek compaction 
    level_stats_[compact->compaction->level()].number_seek_leveling_compactions++;
    level_stats_[compact->compaction->level()].number_seek_leveling_compaction_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_seek_leveling_compaction_participant_files += compact->compaction->num_input_files(1);
  }
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  // 这个 compact->compaction->level() 是指当前 compaction 的 level，也是就是哪个level需要被合并
  assert(versions_->Num_Level_Partitionleveling_Files(compact->compaction->level(), compact->compaction->partition_num()) > 0);
  assert(compact->builder == nullptr);
  assert(compact->outfile == nullptr);
  if (snapshots_.empty()) {
    compact->smallest_snapshot = versions_->LastSequence();
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
  }

  // 制作一个迭代器
  Iterator* input = versions_->MakeInputIterator(compact->compaction);

  // Release mutex while we're actually doing the compaction work
  mutex_.Unlock();

  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;

  bool output_key = false;
  mem_partition_guard *current_partition = nullptr;
  int i = 0;
  Slice prev_key;
  int key_happens = 0;  

  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~
  // PrintPartitions(mem_partitions_);
  std::string new_str(input->key().data(), input->key().size()-8);
  mem_partition_guard* temp_partition = new mem_partition_guard(new_str, new_str);
  Log(options_.leveling_info_log, "Current key:%s ",temp_partition->partition_start_str.c_str());

  if(new_str == "0000000000003875"){
    output_key = true;
  }
  auto it1 = mem_partitions_.upper_bound(temp_partition);
  if(it1 == mem_partitions_.begin()){
    fprintf(stderr, "fatal error!\n");
  }else {
    it1--;
  }
  Log(options_.leveling_info_log, "upper_bound it1 partition: start=%s, end=%s",
    (*it1)->partition_start.ToString().c_str(), (*it1)->partition_end.ToString().c_str());
  auto it = (*it1)->sub_partitions_.upper_bound(temp_partition); 
  if(it == (*it1)->sub_partitions_.begin()){
    (*it)->UpdatePartitionStart(new_str);
  }else if(it != (*it1)->sub_partitions_.end() && it != (*it1)->sub_partitions_.begin()){
    it--;
  }else{
    it--;
  }
  current_partition = *it;

  assert(current_partition->is_key_contains(input->key().data(), input->key().size()-8));
  Log(options_.leveling_info_log, "upper_bound it partition: start=%s, end=%s",
      (*it)->partition_start.ToString().c_str(), (*it)->partition_end.ToString().c_str());
  Log(options_.leveling_info_log, "current_partition: start=%s, end=%s",
      current_partition->partition_start.ToString().c_str(), current_partition->partition_end.ToString().c_str());
  delete temp_partition;
  //  ~~~~~ WZZ's comments for his adding source codes ~~~~~

  while (input->Valid() && !shutting_down_.load(std::memory_order_acquire)) {
    // Prioritize immutable compaction work
    if (has_imm_.load(std::memory_order_relaxed)) {
      const uint64_t imm_start = env_->NowMicros();
      mutex_.Lock();
      if (imm_ != nullptr) {
        CompactLevelingMemTable();
        // Wake up MakeRoomForWrite() if necessary.
        background_work_finished_signal_.SignalAll();
      }
      mutex_.Unlock();
      imm_micros += (env_->NowMicros() - imm_start);
    }


    Slice key = input->key();
    // }
    // if(output_key){
    //   Log(options_.leveling_info_log, "Current key:%s ",key.data());
    // }

    if(current_partition->CompareWithEnd(key.data(), key.size()-8)<0){
      if(compact->builder != nullptr){
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          break;
        }
        if(output_key){
          Log(options_.leveling_info_log, "DoCompactionWork: finishing compaction output file: %s key: %s", status.ToString().c_str(), key.data());
        }
        compact->L1_partitions_.emplace_back(current_partition->partition_num);
      }
      it++;
      current_partition = *it; 
      if(current_partition->CompareWithBegin(key.data(),key.size()-8)>0){
        std::string new_start_str(key.data(), input->key().size()-8);
        current_partition->UpdatePartitionStart(new_start_str);
      }
    }
    // if (compact->compaction->ShouldStopBefore(key) &&
    //     compact->builder != nullptr) {
    //   status = FinishCompactionOutputFile(compact, input);
    //   Log(options_.leveling_info_log, "DoCompactionWork: finished compaction output file: %s", status.ToString().c_str());
    //   if (!status.ok()) {
    //     break;
    //   }
    // }

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
      Log(options_.leveling_compaction_info_log, "Error parsing key: %s", key.ToString().c_str());
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
              0) {
        // First occurrence of this user key
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by an newer entry for same user key
        drop = true;  // (A)
        Log(options_.leveling_compaction_info_log, "Key %s is hidden by a newer entry", ikey.user_key.ToString().c_str());
      } else if (ikey.type == kTypeDeletion &&
                 ikey.sequence <= compact->smallest_snapshot &&
                 compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
        // For this user key:
        // (1) there is no data in higher levels
        // (2) data in lower levels will have larger sequence numbers
        // (3) data in layers that are being compacted here and have
        //     smaller sequence numbers will be dropped in the next
        //     few iterations of this loop (by rule (A) above).
        // Therefore this deletion marker is obsolete and can be dropped.
        drop = true;
        Log(options_.leveling_compaction_info_log, "Key %s is a deletion marker and can be dropped", ikey.user_key.ToString().c_str());
      }

      last_sequence_for_key = ikey.sequence;
    }
    
#if 0
    Log(options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeValue, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif

    if (!drop) {
      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        // Log(options_.leveling_info_log, "DoCompactionWork: Open compaction output file: %s", status.ToString().c_str());
        if (!status.ok()) {
          Log(options_.leveling_info_log, "DoCompactionWork: Error opening compaction output file: %s", status.ToString().c_str());
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());
    }

    input->Next();
    i++;
  }
  Log(options_.leveling_info_log, "We have already executed %d operations!\n", i);

  if (status.ok() && shutting_down_.load(std::memory_order_acquire)) {
    status = Status::IOError("Deleting DB during compaction");
  }

  if (status.ok() && compact->builder != nullptr) {
    Log(options_.leveling_info_log, "DoCompactionWork: Finishing last compaction output file");
    status = FinishCompactionOutputFile(compact, input);
    if (!status.ok()) {
      Log(options_.leveling_info_log, "DoCompactionWork: Error finishing last compaction output file: %s", status.ToString().c_str());
    }
    compact->L1_partitions_.emplace_back(current_partition->partition_num);
  }
  if (status.ok()) {
    Log(options_.leveling_info_log, "DoCompactionWork: we need to execute input->status(), last status: %s", status.ToString().c_str());
    status = input->status();
  }
  delete input;
  input = nullptr;

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros - imm_micros;
  for (int which = 0; which < 2; which++) {

    for (int i = 0; i < compact->compaction->num_input_files(which); i++) {
      stats.bytes_read += compact->compaction->input(which, i)->file_size;
    }
  }

  for (size_t i = 0; i < compact->outputs.size(); i++) {
    stats.bytes_written += compact->outputs[i].file_size;
  }

  mutex_.Lock();
  stats_[compact->compaction->level() + 1].Add(stats);
  level_stats_[compact->compaction->level() + 1].leveling_bytes_read += stats.bytes_read;
  level_stats_[compact->compaction->level() + 1].leveling_bytes_written += stats.bytes_written;

  if (status.ok()) {
    
    status = InstallCompactionResults(compact);
    if (!status.ok()) {
      Log(options_.leveling_info_log, "DoCompactionWork: Error installing from L%d to L%d compaction results: %s",
        compact->compaction->level(), compact->compaction->level()+1, status.ToString().c_str());
    }
  } else {
    Log(options_.leveling_info_log, "DoCompactionWork: Error during compaction: %s", status.ToString().c_str());
  }
  if (!status.ok()) {
    RecordBackgroundError(status);
  }
  VersionSet::LevelSummaryStorage tmp;
  Log(options_.leveling_info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
  return status;
}

namespace {

struct IterState {
  port::Mutex* const mu;
  Version* const version GUARDED_BY(mu);
  MemTable* const mem GUARDED_BY(mu);
  MemTable* const imm GUARDED_BY(mu);

  IterState(port::Mutex* mutex, MemTable* mem, MemTable* imm, Version* version)
      : mu(mutex), version(version), mem(mem), imm(imm) {}
};

static void CleanupIteratorState(void* arg1, void* arg2) {
  IterState* state = reinterpret_cast<IterState*>(arg1);
  state->mu->Lock();
  state->mem->Unref();
  if (state->imm != nullptr) state->imm->Unref();
  state->version->Unref();
  state->mu->Unlock();
  delete state;
}

}  // anonymous namespace

Iterator* DBImpl::NewInternalIterator(const ReadOptions& options,
                                      SequenceNumber* latest_snapshot,
                                      uint32_t* seed) {
  mutex_.Lock();
  *latest_snapshot = versions_->LastSequence();

  // Collect together all needed child iterators
  std::vector<Iterator*> list;
  list.push_back(mem_->NewIterator());
  mem_->Ref();
  if (imm_ != nullptr) {
    list.push_back(imm_->NewIterator());
    imm_->Ref();
  }
  versions_->current()->AddIterators(options, &list);
  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();

  IterState* cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
  internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);

  *seed = ++seed_;
  mutex_.Unlock();
  return internal_iter;
}


std::map<uint64_t, Iterator*> DBImpl::NewMultiInternalIterator(const ReadOptions& options,
                                      SequenceNumber* latest_snapshot,
                                      uint32_t* seed) {
  mutex_.Lock();
  *latest_snapshot = versions_->LastSequence();

  std::map<uint64_t, Iterator*> internal_iter_map;  // 存储每个分区的 internal_iter
  // Collect together all needed child iterators
  std::map<uint64_t, std::vector<Iterator*>> lists;
  Iterator* mem_iterator = mem_->NewIterator();
  Iterator* imm_iterator = nullptr;
  if (imm_ != nullptr) {
    imm_iterator = imm_->NewIterator();
  }

  // list.push_back(mem_iterator);
  // mem_->Ref();
  // if (imm_ != nullptr) {
  //   imm_iterator = imm_->NewIterator();
  //   list.push_back(imm_iterator);
  //   imm_->Ref();
  // }

  // 遍历 mem_partitions_，为每个分区生成迭代器
  Iterator* internal_iter = nullptr;
  for (mem_partition_guard* partition : mem_partitions_) {
    for(mem_partition_guard* sub_partition : partition->sub_partitions_){ 
      lists[sub_partition->partition_num].push_back(mem_iterator);
      mem_->Ref();
      if(imm_iterator != nullptr){
        lists[sub_partition->partition_num].push_back(imm_iterator);
        imm_->Ref();
      }
      versions_->current()->AddIterators(partition->partition_num, sub_partition->partition_num, options, &lists[sub_partition->partition_num]);

      // 输出当前分区的编号（可选）
      fprintf(stdout, "Created iterator for partition number: %lu in parent %lu\n", sub_partition->partition_num, partition->partition_num);
      internal_iter = NewMergingIterator(&internal_comparator_, &lists[sub_partition->partition_num][0], lists[sub_partition->partition_num].size());
      versions_->current()->Ref();
      IterState* cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
      internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);
      internal_iter_map[sub_partition->partition_num] = internal_iter;
    }
  }
  
  *seed = ++seed_;
  mutex_.Unlock();
  return internal_iter_map;
}


Iterator* DBImpl::NewTieringInternalIterator(const ReadOptions& options, SequenceNumber* latest_snapshot,uint32_t* seed){
  mutex_.Lock();
  *latest_snapshot = versions_->LastSequence();

  // Collect together all needed child iterators
  std::vector<Iterator*> list;
  list.push_back(hot_mem_->NewIterator());
  hot_mem_->Ref();
  if (hot_imm_ != nullptr) {
    list.push_back(hot_imm_->NewIterator());
    hot_imm_->Ref();
  }

  versions_->current()->AddTieringIterators(options, &list);

  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();

  IterState* cleanup = new IterState(&mutex_, hot_mem_, hot_imm_, versions_->current());
  internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);

  *seed = ++seed_;
  mutex_.Unlock();
  return internal_iter;
} 
  

Iterator* DBImpl::SinglePartitionNewInternalIterator(uint64_t parent_partition, uint64_t sub_partition, const ReadOptions& options,
                                      SequenceNumber* latest_snapshot,
                                      uint32_t* seed) {
  mutex_.Lock();
  *latest_snapshot = versions_->LastSequence();

  // Collect together all needed child iterators
  std::vector<Iterator*> list;

  list.push_back(mem_->NewIterator());
  mem_->Ref();
  if (imm_ != nullptr) {
    list.push_back(imm_->NewIterator());
    imm_->Ref();
  }

  versions_->current()->AddIterators(parent_partition, sub_partition, options, &list);

  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();

  IterState* cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
  internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);

  *seed = ++seed_;
  mutex_.Unlock();
  return internal_iter;
}

Iterator* DBImpl::TEST_NewInternalIterator() {
  SequenceNumber ignored;
  uint32_t ignored_seed;
  return NewInternalIterator(ReadOptions(), &ignored, &ignored_seed);
}

int64_t DBImpl::TEST_MaxNextLevelOverlappingBytes() {
  MutexLock l(&mutex_);
  return versions_->MaxNextLevelOverlappingBytes();
}

Status DBImpl::Get(const ReadOptions& options, const Slice& key,
                   std::string* value) {
  // PrintPartitions(mem_partitions_);
  // fprintf(stdout, "Current key: %s \n", key.data());
  Status s;
  MutexLock l(&mutex_);
  SequenceNumber snapshot;
  bool is_hot = false;

  if(HotRanges->hot_ranges_.size()!=0){
    hot_range temp_hot_range(key.ToString(),key.ToString());
    auto it = HotRanges->hot_ranges_.upper_bound(temp_hot_range);
    if(it == HotRanges->hot_ranges_.begin() ){
      is_hot = false;
    }else{
      // fprintf(stdout,"There are %lu hot ranges!\n",HotRanges->hot_ranges_.size());
      it--;
      // it->printRange();
      // check if the key is in the hot range
      if(it->is_key_contains(key.data(), key.size())){
        is_hot = true;
      }else{
        is_hot = false;
      }
    }
  }
  
  // if (is_hot) {
  //   fprintf(stdout, "Key is hot. Using hot_mem_ and hot_imm_.\n");
  // } else {
  //   fprintf(stdout, "Key is cold. Using mem_ and imm_.\n");
  // }
  
  if (options.snapshot != nullptr) {
    snapshot = static_cast<const SnapshotImpl*>(options.snapshot)->sequence_number();
  } else {
    snapshot = versions_->LastSequence();
  }

  MemTable* mem = nullptr;
  MemTable* imm = nullptr;
  MemTable* hot_mem = nullptr;
  MemTable* hot_imm = nullptr;

  if (is_hot) {
    hot_mem = hot_mem_;
    hot_imm = hot_imm_;
    hot_mem->Ref();  // Reference hot_mem if is_hot is true
    if(hot_imm != nullptr) hot_imm->Ref();
  }else{
    mem = mem_;
    imm = imm_;
    mem->Ref();
    if (imm != nullptr) imm->Ref();
  }

  Version* current = versions_->current();
  current->Ref();

  bool have_stat_update = false;
  Version::GetStats stats;

  Get_Time_Stats local_stats; // Local instance to store stats for this Get call
  int64_t start_time = env_->NowMicros();
  
  LookupKey lkey(key, snapshot);
  int64_t memtable_time = 0;
  int64_t immtable_time = 0;
  int64_t disk_time = 0;
  bool found_in_memtable = false;
  bool found_in_imm = false;
  bool found_result = true;

  // Unlock while reading from files and memtables
  if(!is_hot){
    mutex_.Unlock();
    // First look in the memtable, then in the immutable memtable (if any).
    // int64_t mem_start_time = env_->NowMicros();
    if (mem->Get(lkey, value, &s)) {
      // local_stats.memtable_time = env_->NowMicros() - mem_start_time;
      found_in_memtable = true;
      // Done
    } else {
      // local_stats.memtable_time = env_->NowMicros() - mem_start_time;
      // int64_t imm_start_time = env_->NowMicros();
      if (imm != nullptr && imm->Get(lkey, value, &s)) {
        // local_stats.immtable_time = env_->NowMicros() - imm_start_time;
        found_in_imm = true;
      } else {
        // local_stats.immtable_time = env_->NowMicros() - imm_start_time;
      }
    } 
    mutex_.Lock();
  }else{
    // int64_t hot_mem_start_time = env_->NowMicros();
    if (hot_mem_->Get(lkey, value, &s)) {
      // local_stats.memtable_time = env_->NowMicros() - hot_mem_start_time;
      found_in_memtable = true;
      // Done for hot memtable
    } else {
      // local_stats.memtable_time = env_->NowMicros() - hot_mem_start_time;
      // int64_t imm_start_time = env_->NowMicros();
      if (hot_imm != nullptr && hot_imm->Get(lkey, value, &s)) {
        // local_stats.immtable_time = env_->NowMicros() - imm_start_time;
        found_in_imm = true;
      } else {
        // local_stats.immtable_time = env_->NowMicros() - imm_start_time;
      }
    }
  }

  // PrintPartitions(mem_partitions_);
  if (!found_in_memtable && !found_in_imm) {
    if(!is_hot){
      // searching cold data
      std::string current_key_str(key.data(), key.size());
      // fprintf(stdout, "Current Key String: %s\n", current_key_str.c_str());
      mem_partition_guard temp_partition(current_key_str, current_key_str);
      auto it = mem_partitions_.upper_bound(&temp_partition);
      if(it==mem_partitions_.begin()){
        found_result = false;
      }else{
        it--;
        if((*it)->is_key_contains(current_key_str.c_str(),current_key_str.size())){
          // PrintPartition(*it);
          auto it2 = (*it)->sub_partitions_.upper_bound(&temp_partition);
          if(it2==(*it)->sub_partitions_.begin()){
            found_result = false;
          }else{
            it2--;
            // PrintPartition(*it2);
            // fprintf(stdout, "searched partition is: %lu sub-partition is: %lu \n", (*it)->partition_num, (*it2)->partition_num);
            // int64_t disk_start_time = env_->NowMicros();
            s = current->Get(options, lkey, value, &stats, (*it)->partition_num, (*it2)->partition_num);
            // disk_time = env_->NowMicros() - disk_start_time;
          }
        }
      }
    }else{
      // int64_t disk_start_time = env_->NowMicros();
      s = current->Get_Hot(options, lkey, value, &stats);
      // disk_time = env_->NowMicros() - disk_start_time;
    }
    have_stat_update = true;
  }

  // local_stats.total_time = env_->NowMicros() - start_time;
  // get_time_stats.Add(local_stats);  // Accumulate stats in the class-level instance
  
  if (is_hot) {
    hot_mem->Unref();
    if (hot_imm != nullptr) hot_imm->Unref();
  }else{
    mem->Unref();
    if (imm != nullptr) imm->Unref();
  }
  
  current->Unref();
  // fprintf(stdout,"End!\n\n\n");

  if(found_result == false){
    return Status::NotFound(Slice());
  }

  return s;
}

Iterator* DBImpl::NewIterator(const ReadOptions& options) {
  SequenceNumber latest_snapshot;
  uint32_t seed;


  Iterator* iter = NewInternalIterator(options, &latest_snapshot, &seed);

  return NewDBIterator(this, user_comparator(), iter,
                       (options.snapshot != nullptr
                            ? static_cast<const SnapshotImpl*>(options.snapshot)
                                  ->sequence_number()
                            : latest_snapshot),
                       seed);
}

std::map<uint64_t, Iterator*> DBImpl::NewMultiIterator(const ReadOptions& options) {
  SequenceNumber latest_snapshot;
  uint32_t seed;


  std::map<uint64_t, Iterator*> internal_iters = NewMultiInternalIterator(options, &latest_snapshot, &seed);
  Iterator* tiering_iters = NewTieringInternalIterator(options, &latest_snapshot, &seed);
  SequenceNumber seq= options.snapshot != nullptr ? static_cast<const SnapshotImpl*>(options.snapshot)->sequence_number() : latest_snapshot;

  // 创建一个 map，用于存储每个分区的 DBIterator
  std::map<uint64_t, Iterator*> db_iter_map;
  Iterator* tiering_db_iters = NewDBIterator(this, user_comparator(), tiering_iters,seq,seed);
  db_iter_map[0]=tiering_db_iters;

  
  for (auto& entry : internal_iters) {
    uint64_t partition_num = entry.first;
    Iterator* internal_iter = entry.second;

    // 创建一个 DBIterator，并将其添加到 db_iter_map
    Iterator* db_iter = NewDBIterator( this, user_comparator(), internal_iter,seq,seed);
        
    db_iter_map[partition_num] = db_iter;
  }

  return db_iter_map;
}



// |--------------------|------------------|-------------------|
//     Hot Range 1         Hot Range 2           Key Range
//   (Start, End)      (Start, End)        (Key)
  
//     ^------>        ^------------------^
//     (smallest)       ^----> Key Point <------> (Largest)
//       Key               Hot Ranges                    Key > Largest Hot Range

int DBImpl::Is_Overlap_HotRanges(const Slice& key){

  if(HotRanges->hot_ranges_.size()!=0){
    hot_range temp_hot_range(key.ToString(),key.ToString());
    auto it = HotRanges->hot_ranges_.upper_bound(temp_hot_range);

    if(it == HotRanges->hot_ranges_.end()){
      it--;
      if(it->is_key_contains(key.data(), key.size())){
        return  0;
      }else{
        return 1;
      }
    }else{
      return  0;
    }
  }
  return 1;
}

Iterator* DBImpl::FindIteratorByKey(const std::map<uint64_t, Iterator*>& iter_map, const Slice& start_key) {

  // PrintPartitions(mem_partitions_);
  int key_situation=Is_Overlap_HotRanges(start_key);

  if(key_situation == 0){
    Iterator * tiering_iterator = iter_map.at(0);
  }

  // fprintf(stdout, "Key: %s  situation:%d \n", start_key.data(), key_situation);

  uint64_t start_parent_partiton, start_sub_partition;
  std::string current_key_str(start_key.data(), start_key.size());
  // fprintf(stdout, "Current Key String: %s\n", current_key_str.c_str());
  mem_partition_guard temp_partition(current_key_str, current_key_str);
  auto it = mem_partitions_.upper_bound(&temp_partition);

  if(it==mem_partitions_.begin()){
    start_parent_partiton = (*it)->partition_num;
    auto it2 = (*it)->sub_partitions_.begin();
    start_sub_partition = (*it2)->partition_num;
    // fprintf(stdout, "Key: %s, start_parent_partiton: %lu (upper_bound begin) start_sub_partition %lu\n", 
    //           current_key_str.c_str(), start_parent_partiton, start_sub_partition);
  }else if(it==mem_partitions_.end()){
    it--;
    if((*it)->is_key_contains(current_key_str.c_str(),current_key_str.size())){
      // PrintPartition(*it);
      start_parent_partiton = (*it)->partition_num;
      auto it2 = (*it)->sub_partitions_.upper_bound(&temp_partition);
      it2--;
      start_sub_partition = (*it2)->partition_num;
      // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (upper_bound end)\n", 
      //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
    }else{
      fprintf(stdout, "Key: %s not in any partition.\n", current_key_str.c_str());
      return nullptr;
    }
  }else{
    it--;
    if((*it)->is_key_contains(current_key_str.c_str(),current_key_str.size())){
      start_parent_partiton = (*it)->partition_num;
      auto it2 = (*it)->sub_partitions_.upper_bound(&temp_partition);
      it2--;
      start_sub_partition = (*it2)->partition_num;
      // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (in middle partition)\n", 
      //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
    }else{
      it++;
      start_parent_partiton = (*it)->partition_num;
      auto it2 = (*it)->sub_partitions_.begin();
      start_sub_partition = (*it2)->partition_num;
      // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (upper_bound middle)\n", 
      //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
    }
  }

  auto it3 = iter_map.find(start_sub_partition);
  if (it3 != iter_map.end()) {
    // 如果找到，返回对应的 Iterator
    return it3->second;
  } else {
    // 如果未找到，返回 nullptr
    fprintf(stdout, "Key: %s not foud in iter_map.\n", start_key.ToString().c_str());
    return nullptr;
  }
}


Iterator* DBImpl::NewIterator(const ReadOptions& options, const Slice& start_key) {

  SequenceNumber latest_snapshot;
  uint32_t seed;
  // PrintPartitions(mem_partitions_);
  int key_situation=Is_Overlap_HotRanges(start_key);
  // fprintf(stdout, "Key: %s  situation:%d \n", start_key.data(), key_situation);

  uint64_t start_parent_partiton, start_sub_partition;
  if(key_situation==1){
    std::string current_key_str(start_key.data(), start_key.size());
    // fprintf(stdout, "Current Key String: %s\n", current_key_str.c_str());
    mem_partition_guard temp_partition(current_key_str, current_key_str);
    auto it = mem_partitions_.upper_bound(&temp_partition);

    if(it==mem_partitions_.begin()){
      start_parent_partiton = (*it)->partition_num;
      auto it2 = (*it)->sub_partitions_.begin();
      start_sub_partition = (*it2)->partition_num;
      // fprintf(stdout, "Key: %s, start_parent_partiton: %lu (upper_bound begin) start_sub_partition %lu\n", 
      //           current_key_str.c_str(), start_parent_partiton, start_sub_partition);
    }else if(it==mem_partitions_.end()){
      it--;
      if((*it)->is_key_contains(current_key_str.c_str(),current_key_str.size())){
        // PrintPartition(*it);
        start_parent_partiton = (*it)->partition_num;
        auto it2 = (*it)->sub_partitions_.upper_bound(&temp_partition);
        it2--;
        start_sub_partition = (*it2)->partition_num;
        // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (upper_bound end)\n", 
        //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
      }else{
        // fprintf(stdout, "Key: %s not in any partition.\n", current_key_str.c_str());
        return nullptr;
      }
    }else{
      it--;
      if((*it)->is_key_contains(current_key_str.c_str(),current_key_str.size())){
        start_parent_partiton = (*it)->partition_num;
        auto it2 = (*it)->sub_partitions_.upper_bound(&temp_partition);
        it2--;
        start_sub_partition = (*it2)->partition_num;
        // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (in middle partition)\n", 
        //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
      }else{
        it++;
        start_parent_partiton = (*it)->partition_num;
        auto it2 = (*it)->sub_partitions_.begin();
        start_sub_partition = (*it2)->partition_num;
        // fprintf(stdout, "Key: %s, start_parent_partiton: %lu, start_sub_partition: %lu (upper_bound middle)\n", 
        //             current_key_str.c_str(), start_parent_partiton, start_sub_partition);
      }
    }
  }else{
    // fprintf(stdout, "Key: %s is in hot ranges.\n", start_key.ToString().c_str());
  }

  Iterator* iter = SinglePartitionNewInternalIterator(start_parent_partiton, start_sub_partition, options, &latest_snapshot, &seed);

  return NewDBIterator(this, user_comparator(), iter,
                       (options.snapshot != nullptr
                            ? static_cast<const SnapshotImpl*>(options.snapshot)
                                  ->sequence_number()
                            : latest_snapshot),
                       seed);
}

void DBImpl::RecordReadSample(Slice key) {
  MutexLock l(&mutex_);
  if (versions_->current()->RecordReadSample(key)) {
    MaybeScheduleCompaction();
  }
}

const Snapshot* DBImpl::GetSnapshot() {
  MutexLock l(&mutex_);
  return snapshots_.New(versions_->LastSequence());
}

void DBImpl::ReleaseSnapshot(const Snapshot* snapshot) {
  MutexLock l(&mutex_);
  snapshots_.Delete(static_cast<const SnapshotImpl*>(snapshot));
}

// Convenience methods
Status DBImpl::Put(const WriteOptions& o, const Slice& key, const Slice& val) {
  return DB::Put(o, key, val);
}

Status DBImpl::Batch_Put(const WriteOptions& opt, const Slice& key, const Slice& value) {
  return DB::Batch_Put(opt, key, value);
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {
  return DB::Delete(options, key);
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
  Writer w(&mutex_);
  w.batch = updates; // 记录要写入的数据
  w.sync = options.sync; // 记录要写入的选项，只有是否同步一个选项
  w.done = false;    // 写入的状态，完成或者未完成，当前肯定是未完成
      
  // mutex_是leveldb的全局锁，在DBImpl有且只有这一个互斥锁(还有一个文件锁除外)，所有操作都要基于这一个锁实现互斥
  // MutexLock是个自动锁，他的构造函数负责加锁，析构函数负责解锁
  MutexLock l(&mutex_);

  // writers_是个std::deque<Writer*>，是DBImpl的成员变量，也就意味着多线程共享这个变量，所以是在加锁状态下操作的
  writers_.push_back(&w);

  // 这段代码保证了写入是按照调用的先后顺序执行的。
  // 1.w.done不可能是true啊，刚刚赋值为false，为什么还要判断呢？除非有人改动，没错，后面有可能会被其他线程改动
  // 2.刚刚放入队列尾部，此时如果前面有线程写，那么&w != writers_.front()会为true，所以要等前面的写完在唤醒
  // 3.w.cv是一个可以理解为pthread_cond_t的变量，w.cv.Wait()其实是需要解锁的，他解的就是mutex_这个锁
  while (!w.done && &w != writers_.front()) {
    w.cv.Wait();
  }

  // 刚刚也提到了，虽然期望是自己线程把自己的数据写入，因为数据放入了writers_这个队列中，也就意味着别的线程也能看到
  // 也就意味着别的线程也能把这个数据写入，那么什么情况要需要其他线程帮这个线程写入呢？
  if (w.done) {
    return w.status;
  }

  // May temporarily unlock and wait.
  // 这个就是要判断当前的空间是否能够继续写入，包括MemTable以及SSTable，如果需要同步文件或者合并文件就要等待了
  Status status = MakeRoomForWrite(updates == nullptr);

  // 获取当前最后一个顺序号，这个好理解哈
  uint64_t last_sequence = versions_->LastSequence();

  // 接下来就是比较重点的部分了，last_writer记录了一次真正写入的最后一个Writer的地址，就是会合并多个Writer的数据写入
  // 当然，初始化是当前这个线程的Writer，因为很可能后面没有其他线程执行写入
  Writer* last_writer = &w;
  // 开始写入之前需要保证空间足够并且确实有数据要写
  if (status.ok() && updates != nullptr) {  // nullptr batch is for compactions
    // 此处就是合并写入的过程，函数名字也能看出这个意思，
    WriteBatch* write_batch = BuildBatchGroup(&last_writer);
    // 这个就是设置写入的顺序号，这个顺序号是全局的，每次写入都会增加

    WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);
    // 更新顺序号，先记在临时变量中，等操作全部成功后再更新数据库状态
    last_sequence += WriteBatchInternal::Count(write_batch);

    // Add to log and apply to memtable.  We can release the lock
    // during this phase since &w is currently responsible for logging
    // and protects against concurrent loggers and concurrent writes
    // into mem_.
    {
      mutex_.Unlock();
      status = log_->AddRecord(WriteBatchInternal::Contents(write_batch));
      bool sync_error = false;
      if (status.ok() && options.sync) {
        status = logfile_->Sync();
        if (!status.ok()) {
          sync_error = true;
        }
      }

      // write data into the memtable and hot memtable if it is hot data
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(write_batch, mem_, hot_mem_, options_.info_log);
      }

      mutex_.Lock();
      if (sync_error) {
        // The state of the log file is indeterminate: the log record we
        // just added may or may not show up when the DB is re-opened.
        // So we force the DB into a mode where all future writes fail.
        RecordBackgroundError(status);
      }
    }
    if (write_batch == tmp_batch_) tmp_batch_->Clear();

    versions_->SetLastSequence(last_sequence);
  }

  while (true) {
    Writer* ready = writers_.front();
    writers_.pop_front();
    if (ready != &w) {
      ready->status = status;
      ready->done = true;
      ready->cv.Signal();
    }
    if (ready == last_writer) break;
  }

  // Notify new head of write queue
  if (!writers_.empty()) {
    writers_.front()->cv.Signal();
  }

  return status;
}


Status DBImpl::Write(const WriteOptions& options, const Slice& key, const Slice& value){
  MutexLock l(&mutex_);
  
  // if(key.size() > 16){
  //   fprintf(stderr,"key size:%ld, value size:%ld\n", key.size(), value.size());
  // }
  
  int64_t start_time = env_->NowMicros();
  if(in_memory_batch_kv_number<10000){
    // fprintf(stderr,"in_memory_batch count: %d, hot_key_identifier count: %d\n", WriteBatchInternal::Count(in_memory_batch), hot_key_identifier->get_current_num_kv());
    in_memory_batch->Put(key, value);
    in_memory_batch_kv_number++;
    // hot_key_identifier->add_data(key);
    // std::string test_key = key.ToString();
    // auto* data = new std::pair<DBImpl*, std::string>(this, test_key);
    // env_->Schedule(&DBImpl::AddDataBGWork, data);
    return Status::OK();
  }
  int64_t end_time = env_->NowMicros();
  identifier_time += (end_time - start_time);
  

  // if(total_number < 10000){
  //   // fprintf(stderr,"in_memory_batch count: %d, hot_key_identifier count: %d\n", WriteBatchInternal::Count(in_memory_batch), hot_key_identifier->get_current_num_kv());
  //   auto it = batch_data_.find(key.ToString());
  //   if (it != batch_data_.end()) {
  //       it->second += 1;
  //   } else {
  //       // if not found, insert new key with count 1
  //       batch_data_[key.ToString()] = 1;
  //   }

  //   in_memory_batch->Put(key, value);
  //   total_number++;
  //   // hot_key_identifier->add_data(key);
  //   return Status::OK();
  // }


  Writer w(&mutex_);
  w.batch = in_memory_batch; 
  w.sync = options.sync; 
  w.done = false;          

  // writers_是个std::deque<Writer*>，是DBImpl的成员变量，也就意味着多线程共享这个变量，所以是在加锁状态下操作的
  writers_.push_back(&w);

  // 这段代码保证了写入是按照调用的先后顺序执行的。
  // 1.w.done不可能是true啊，刚刚赋值为false，为什么还要判断呢？除非有人改动，没错，后面有可能会被其他线程改动
  // 2.刚刚放入队列尾部，此时如果前面有线程写，那么&w != writers_.front()会为true，所以要等前面的写完在唤醒
  // 3.w.cv是一个可以理解为pthread_cond_t的变量，w.cv.Wait()其实是需要解锁的，他解的就是mutex_这个锁
  while (!w.done && &w != writers_.front()) {
    w.cv.Wait();
  }

  // 刚刚也提到了，虽然期望是自己线程把自己的数据写入，因为数据放入了writers_这个队列中，也就意味着别的线程也能看到
  // 也就意味着别的线程也能把这个数据写入，那么什么情况要需要其他线程帮这个线程写入呢？
  if (w.done) {
    return w.status;
  }

  // May temporarily unlock and wait.
  // 这个就是要判断当前的空间是否能够继续写入，包括MemTable以及SSTable，如果需要同步文件或者合并文件就要等待了
  Status status = MakeRoomForWrite(in_memory_batch == nullptr);

  // 获取当前最后一个顺序号，这个好理解哈
  uint64_t last_sequence = versions_->LastSequence();

  // 接下来就是比较重点的部分了，last_writer记录了一次真正写入的最后一个Writer的地址，就是会合并多个Writer的数据写入
  // 当然，初始化是当前这个线程的Writer，因为很可能后面没有其他线程执行写入
  Writer* last_writer = &w;
  // 开始写入之前需要保证空间足够并且确实有数据要写
  if (status.ok() && in_memory_batch != nullptr) {  // nullptr batch is for compactions
    // 此处就是合并写入的过程，函数名字也能看出这个意思，
    WriteBatch* write_batch = BuildBatchGroup(&last_writer);
    // 这个就是设置写入的顺序号，这个顺序号是全局的，每次写入都会增加

    WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);
    // 更新顺序号，先记在临时变量中，等操作全部成功后再更新数据库状态
    last_sequence += WriteBatchInternal::Count(write_batch);

    // Add to log and apply to memtable.  We can release the lock
    // during this phase since &w is currently responsible for logging
    // and protects against concurrent loggers and concurrent writes
    // into mem_.
    {
      mutex_.Unlock();
      status = log_->AddRecord(WriteBatchInternal::Contents(in_memory_batch));
      bool sync_error = false;
      if (status.ok() && options.sync) {
        status = logfile_->Sync();
        if (!status.ok()) {
          sync_error = true;
        }
      }

      // write data into the memtable and hot memtable if it is hot data
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(in_memory_batch, mem_, hot_mem_, options_.info_log, HotRanges);
      }

      mutex_.Lock();
      if (sync_error) {
        // The state of the log file is indeterminate: the log record we
        // just added may or may not show up when the DB is re-opened.
        // So we force the DB into a mode where all future writes fail.
        RecordBackgroundError(status);
      }
    }
    if (write_batch == tmp_batch_) tmp_batch_->Clear();
    versions_->SetLastSequence(last_sequence);
  }

  while (true) {
    Writer* ready = writers_.front();
    writers_.pop_front();
    if (ready != &w) {
      ready->status = status;
      ready->done = true;
      ready->cv.Signal();
    }
    if (ready == last_writer) break;
  }

  // Notify new head of write queue
  if (!writers_.empty()) {
    writers_.front()->cv.Signal();
  }
  in_memory_batch->Clear();
  batch_data_.clear();
  in_memory_batch_kv_number = 0;
  return status;
}

// REQUIRES: Writer list must be non-empty
// REQUIRES: First writer must have a non-null batch
WriteBatch* DBImpl::BuildBatchGroup(Writer** last_writer) {
  mutex_.AssertHeld();
  assert(!writers_.empty());

  Writer* first = writers_.front(); // 获取写请求队列中的第一个写请求
  WriteBatch* result = first->batch;
  assert(result != nullptr);

  size_t size = WriteBatchInternal::ByteSize(first->batch); // 计算第一个 WriteBatch 的大小。

  // Allow the group to grow up to a maximum size, but if the
  // original write is small, limit the growth so we do not slow
  // down the small write too much.
  size_t max_size = 1 << 20;
  if (size <= (128 << 10)) {
    max_size = size + (128 << 10);
  }

  *last_writer = first;
  std::deque<Writer*>::iterator iter = writers_.begin();
  ++iter;  // Advance past "first"

  for (; iter != writers_.end(); ++iter) {
    Writer* w = *iter;
    if (w->sync && !first->sync) {
      // Do not include a sync write into a batch handled by a non-sync write.
      break;
    }

    if (w->batch != nullptr) {
      size += WriteBatchInternal::ByteSize(w->batch);
      if (size > max_size) {
        // Do not make batch too big
        break;
      }

      // Append to *result
      if (result == first->batch) {
        // Switch to temporary batch instead of disturbing caller's batch
        result = tmp_batch_;
        assert(WriteBatchInternal::Count(result) == 0);
        WriteBatchInternal::Append(result, first->batch);
      }
      WriteBatchInternal::Append(result, w->batch);
    }
    *last_writer = w;
  }
  return result;
}

// REQUIRES: mutex_ is held
// REQUIRES: this thread is currently at the front of the writer queue
Status DBImpl::MakeRoomForWrite(bool force) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  bool allow_delay = !force;
  Status s;
  while (true) {
    if (!bg_error_.ok()) {
      // Yield previous error
      s = bg_error_;
      break;
    } else if (allow_delay && versions_->NumLevelFiles(0) >=
                                  config::kL0_SlowdownWritesTrigger*config::kTiering_and_leveling_Multiplier) {  
      // We are getting close to hitting a hard limit on the number of
      // L0 files.  Rather than delaying a single write by several
      // seconds when we hit the hard limit, start delaying each
      // individual write by 1ms to reduce latency variance.  Also,
      // this delay hands over some CPU to the compaction thread in
      // case it is sharing the same core as the writer.
      mutex_.Unlock();
      env_->SleepForMicroseconds(1000);
      allow_delay = false;  // Do not delay a single write more than once
      mutex_.Lock();
    } else if (!force &&
               (mem_->ApproximateMemoryUsage() <= options_.write_buffer_size) && 
               (hot_mem_ == nullptr || hot_mem_->ApproximateMemoryUsage() <= options_.write_hot_buffer_size)) {
      // There is room in current memtable
      break;
    } else if (imm_ != nullptr || hot_imm_ != nullptr) {
      // We have filled up the current memtable, but the previous
      // one is still being compacted, so we wait.
      Log(options_.info_log, "Current memtable full; waiting...\n");
      background_work_finished_signal_.Wait();
    } else if (versions_->Num_Level_maxPartitionleveling_Files(0) >= config::kInitialPartitionLevelingCompactionTrigger ) {
      // There are too many level-0 leveling files.
      Log(options_.info_log, "Too many L0 leveling files; waiting...\n");
      background_work_finished_signal_.Wait();
    }else if (versions_->Num_Level_tiering_Files(0) >= config::kTiering_and_leveling_Multiplier) {
      // There are too many level-0 tiering files.
      Log(options_.info_log, "Too many L0 tiering files; waiting... NumLevel_tiering_Files = %d, kTiering_and_leveling_Multiplier = %d\n",
          versions_->Num_Level_tiering_Files(0), config::kTiering_and_leveling_Multiplier);
      background_work_finished_signal_.Wait();
    } else {
      // Attempt to switch to a new memtable and trigger compaction of old
      assert(versions_->PrevLogNumber() == 0);
      //确保没有前一个日志文件。这是一种安全检查，确保我们没有未完成的日志文件。

      uint64_t new_log_number = versions_->NewFileNumber();
      // fprintf(stderr, "new_log_number: %lu in MakeRoomForWrite\n", new_log_number);
      // 生成一个新的文件编号，用于新的 WAL 文件。

      WritableFile* lfile = nullptr;
      //声明一个指向新的 WAL 文件的指针。

      s = env_->NewWritableFile(LogFileName(dbname_, new_log_number), &lfile);
      //创建一个新的 WAL 文件，文件名为根据 new_log_number 生成的
      if (!s.ok()) {
        // Avoid chewing through file number space in a tight loop.
        versions_->ReuseFileNumber(new_log_number);
        break;
      }

      delete log_;

      s = logfile_->Close();
      if (!s.ok()) {
        // We may have lost some data written to the previous log file.
        // Switch to the new log file anyway, but record as a background
        // error so we do not attempt any more writes.
        //
        // We could perhaps attempt to save the memtable corresponding
        // to log file and suppress the error if that works, but that
        // would add more complexity in a critical code path.
        RecordBackgroundError(s);
      }
      delete logfile_;

      logfile_ = lfile;
      logfile_number_ = new_log_number;
      log_ = new log::Writer(lfile);
      // Log(options_.info_log, "memory immutable(size:%lu) write_buffer_size:%lu ",mem_->ApproximateMemoryUsage(),options_.write_buffer_size);

      if(mem_ != nullptr && mem_->ApproximateMemoryUsage()>options_.write_buffer_size){
        imm_ = mem_;
        has_imm_.store(true, std::memory_order_release);
        mem_ = new MemTable(internal_comparator_);
        Log(options_.info_log, "In-memory immutable(size:%lu) is saturated!",imm_->ApproximateMemoryUsage());
        mem_->Ref();
      } 
      
      if(hot_mem_ != nullptr && hot_mem_->ApproximateMemoryUsage() > options_.write_hot_buffer_size){
        hot_imm_ = hot_mem_;
        has_hot_imm_.store(true, std::memory_order_release);
        hot_mem_ = new MemTable(internal_comparator_);
        Log(options_.info_log, "In-memory hot immutable(size:%lu) is saturated!",hot_imm_->ApproximateMemoryUsage());
        hot_mem_->Ref();
      }

      force = false;  // Do not force another compaction if have room
      MaybeScheduleCompaction();
    }
  }
  return s;
}

bool DBImpl::GetProperty(const Slice& property, std::string* value) {
  
  value->clear();
  MutexLock l(&mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level >= config::kNumLevels) {
      return false;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%d",
                    versions_->NumLevelFiles(static_cast<int>(level)));
      *value = buf;
      return true;
    }
  } else if (in == "stats") {
    double user_io = 0;
    double total_io = 0;
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "                               Compactions\n"
                  "Level  Files Size(MB) Time(sec) Read(MB) Write(MB)\n"
                  "--------------------------------------------------\n");
    value->append(buf);
    fprintf(stderr, "entering io_statistics1\n");
    fflush(stdout);
    for (int level = 0; level < config::kNumLevels; level++) {
      int files = versions_->NumLevelFiles(level);
      if (stats_[level].micros > 0 || files > 0) {
        std::snprintf(buf, sizeof(buf), "%3d %8d %8.0f %9.0f %8.0f %9.0f\n",
                      level, files, versions_->NumLevelBytes(level) / 1048576.0,
                      stats_[level].micros / 1e6,
                      stats_[level].bytes_read / 1048576.0,
                      stats_[level].bytes_written / 1048576.0);
        value->append(buf);
      }
      total_io += stats_[level].bytes_written / 1048576.0;
      if(level == 0){
        user_io = stats_[level].bytes_written/ 1048576.0;
      }
    }
    // fprintf(stderr, "entering io_statistics\n");
    // fflush(stdout);
    snprintf(buf, sizeof(buf), "user_io:%.3fMB total_ios: %.3fMB WriteAmplification: %2.4f\n", user_io, total_io, total_io/ user_io);
    value->append(buf);
    return true;
  } else if (in == "sstables") {
    *value = versions_->current()->DebugString();
    return true;
  } else if (in == "approximate-memory-usage") {
    size_t total_usage = options_.block_cache->TotalCharge();
    if (mem_) {
      total_usage += mem_->ApproximateMemoryUsage();
    }
    if (imm_) {
      total_usage += imm_->ApproximateMemoryUsage();
    }
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(total_usage));
    value->append(buf);
    return true;
  }

  return false;
}


bool DBImpl::GetProperty_with_whole_lsm(const Slice& property, std::string* value) {
  
  value->clear();
  MutexLock l(&mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level >= config::kNumLevels) {
      return false;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%d",
                    versions_->NumLevelFiles(static_cast<int>(level)));
      *value = buf;
      return true;
    }
  } else if (in == "stats") {

    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
    // I modified this part for print more details of a whole LSM
    double user_io = 0;
    double total_io = 0;
    char buf[300];
    fprintf(stderr, "all identification time:%f\n",identifier_time/1e6);
    std::snprintf(buf, sizeof(buf),
                  "                               Compactions\n"
                  "Level Files(Tier) Size(M) Time(s) Read(L) Read(T) Write(L) Write(T) m_comp si_comp(Tiering) ifile(Tiering) pfile(Tiering) se_comp(Tiering) ifiles(Tiering) pfiles(Tiering) comps triv_move t_last_b t_next_b\n"
                  "--------------------------------------------------\n");
    value->append(buf);
    for (int level = 0; level < config::kNumLevels; level++) {
      total_io += stats_[level].bytes_written / 1048576.0;
      if(level == 0){
        user_io = stats_[level].bytes_written/ 1048576.0;
      }
      int files = versions_->Num_Level_Partitionleveling_Files(level);
      int tierfiles = versions_->Num_Level_tiering_Files(level);
      if (stats_[level].micros > 0 || files > 0 || tierfiles >0) {
        std::vector<int> percents = GetLevelPercents();
        std::snprintf(buf, sizeof(buf), "%5d %5d(%4d) %7.0f %7.0f %7.0f %7.0f %8.0f %8.0f %6d %7d(%7d) %5d(%7d) %5d(%7d) %7d(%7d) %6d(%7d) %6d(%7d) %5d %9d %8.0f %8.0f\n",
                      level, files,tierfiles, versions_->NumLevelBytes(level) / 1048576.0,
                      stats_[level].micros / 1e6,
                      level_stats_[level].leveling_bytes_read / 1048576.0,
                      level_stats_[level].tiering_bytes_read / 1048576.0,            
                      level_stats_[level].leveling_bytes_written / 1048576.0,
                      level_stats_[level].tiering_bytes_written / 1048576.0,                      
                      level_stats_[level].number_manual_compaction,
                      level_stats_[level].number_size_leveling_compactions,
                      level_stats_[level].number_size_tieirng_compactions,
                      level_stats_[level].number_size_compaction_leveling_initiator_files,
                      level_stats_[level].number_size_compaction_tieirng_initiator_files,
                      level_stats_[level].number_size_compaction_leveling_participant_files,
                      level_stats_[level].number_size_compaction_tieirng_participant_files,
                      level_stats_[level].number_seek_leveling_compactions,
                      level_stats_[level].number_seek_tiering_compactions,
                      level_stats_[level].number_seek_leveling_compaction_initiator_files,
                      level_stats_[level].number_seek_tiering_compaction_initiator_files,
                      level_stats_[level].number_seek_leveling_compaction_participant_files,
                      level_stats_[level].number_seek_tiering_compaction_participant_files,
                      level_stats_[level].number_size_leveling_compactions+level_stats_[level].number_size_tieirng_compactions,
                      level_stats_[level].number_TrivialMove,
                      level_stats_[level].moved_directly_from_last_level_bytes / 1048576.0,
                      level_stats_[level].moved_from_this_level_bytes / 1048576.0);
        value->append(buf); 
      }
    }
    snprintf(buf, sizeof(buf), "user_io:%.3fMB total_ios: %.3fMB WriteAmplification: %2.4f", user_io, total_io, total_io/ user_io);
    value->append(buf);
    return true;
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  } else if (in == "sstables") {
    *value = versions_->current()->DebugString();
    return true;
  } else if (in == "approximate-memory-usage") {
    size_t total_usage = options_.block_cache->TotalCharge();
    if (mem_) {
      total_usage += mem_->ApproximateMemoryUsage();
    }
    if (imm_) {
      total_usage += imm_->ApproximateMemoryUsage();
    }
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(total_usage));
    value->append(buf);
    return true;
  }

  return false;
}


bool DBImpl::GetProperty_with_read(const Slice& property, std::string* value) {
  
  value->clear();
  MutexLock l(&mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level >= config::kNumLevels) {
      return false;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%d",
                    versions_->NumLevelFiles(static_cast<int>(level)));
      *value = buf;
      return true;
    }
  } else if (in == "stats") {

    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
    // I modified this part for print more details of a whole LSM

    char buf[500];
    // 计算所有时间统计的总和
    double total_time_sum =0.0;
    total_time_sum += 
      versions_->search_stats.level0_search_time / 1000000.0 +
      versions_->search_stats.other_levels_search_time / 1000000.0 +
      table_cache_->table_cache_time_stats.cache_lookup_time / 1000000.0 +
      table_cache_->table_cache_time_stats.disk_load_time / 1000000.0 +
      table_cache_->table_cache_time_stats.table_creation_time / 1000000.0 +
      global_stats.index_block_seek_time.load(std::memory_order_relaxed) / 1000000.0 +
      global_stats.filter_check_time.load(std::memory_order_relaxed) / 1000000.0 +
      global_stats.block_load_seek_time.load(std::memory_order_relaxed) / 1000000.0 +
      get_time_stats.disk_time / 1000000.0;
      std::snprintf(buf, sizeof(buf),
              "Mem    | Imm    | L0Meta  | LoMeta  | Cache   | I/O    | Table_Cre | in_Bl_Se  | Fil_Ch   | Bl_Lo    | Disk    | Total\n"
              "-------------------------------------------------------------------------------------------------------------\n"
              "%6.3f | %6.3f | %6.3f  | %6.3f  | %6.3f  | %6.3f | %8.3f  |%8.3f   | %6.3f   | %6.3f   | %6.3f(%.3f)(%.3f)(%.3f)  | %6.3f\n",
              get_time_stats.memtable_time / 1000000.0, 
              get_time_stats.immtable_time / 1000000.0,
              versions_->search_stats.level0_search_time / 1000000.0,
              versions_->search_stats.other_levels_search_time / 1000000.0,
              table_cache_->table_cache_time_stats.cache_lookup_time / 1000000.0,
              table_cache_->table_cache_time_stats.disk_load_time / 1000000.0,
              table_cache_->table_cache_time_stats.table_creation_time / 1000000.0,
              global_stats.index_block_seek_time.load(std::memory_order_relaxed) / 1000000.0,
              global_stats.filter_check_time.load(std::memory_order_relaxed) / 1000000.0,
              global_stats.block_load_seek_time.load(std::memory_order_relaxed) / 1000000.0,
              get_time_stats.disk_time / 1000000.0, 
              versions_->search_stats.total_time /1000000.0,
              table_cache_->table_cache_time_stats.total_time/1000000.0,
              global_stats.taotal_time/1000000.0,
              get_time_stats.total_time / 1000000.0);


    value->append(buf);
    return true;
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  } else if (in == "sstables") {
    *value = versions_->current()->DebugString();
    return true;
  } else if (in == "approximate-memory-usage") {
    size_t total_usage = options_.block_cache->TotalCharge();
    if (mem_) {
      total_usage += mem_->ApproximateMemoryUsage();
    }
    if (imm_) {
      total_usage += imm_->ApproximateMemoryUsage();
    }
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(total_usage));
    value->append(buf);
    return true;
  }

  return false;
}


Status DBImpl::RestoreHotRangesFromFile(const std::string& dbname_1){

  std::string file_path = dbname_1 + "/MetaHotRange";
  std::ifstream ifs(file_path);

  if (!ifs) {
    fprintf(stderr, "Error opening file for restoring hot ranges: %s!\n", file_path.c_str());
    return Status::NotFound("RangeFile not found");
  }

  nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);  // 解析时不抛出异常
  if (root.is_discarded()) {
    fprintf(stderr, "Error parsing JSON from %s: invalid JSON format.\n", file_path.c_str());
    return Status::Corruption("Error parsing JSON: invalid format");
  }

  // 恢复每个 hot_range
  HotRanges->hot_ranges_.clear();  // 清空原来的数据
  range_boundaries.clear();

  for (const auto& hot_range_json : root["hot_ranges"]) {
    std::string start_str = hot_range_json["start"];
    std::string end_str = hot_range_json["end"];

    range_boundaries.push_back(start_str);
    range_boundaries.push_back(end_str);

    hot_range new_hot_range(
      range_boundaries[range_boundaries.size() - 2].data(), 
      range_boundaries[range_boundaries.size() - 2].size(),
      range_boundaries[range_boundaries.size() - 1].data(),
      range_boundaries[range_boundaries.size() - 1].size()
    );

    HotRanges->hot_ranges_.insert(new_hot_range);
  }

  // 恢复 largest_intensity_range
  if (root.contains("largest_intensity_range")) {
    HotRanges->largestindensity_start_str = root["largest_intensity_range"]["start"];
    HotRanges->largestindensity_end_str= root["largest_intensity_range"]["end"];
    HotRanges->largest_intensity_range = new hot_range(
      HotRanges->largestindensity_start_str.data(),
      HotRanges->largestindensity_start_str.size(),
      HotRanges->largestindensity_end_str.data(),
      HotRanges->largestindensity_end_str.size() );
  }

  // 恢复 min_max_range
  if (root.contains("min_max_range")) {
    HotRanges->min_max_range = new hot_range(
      range_boundaries[0].data(), 
      range_boundaries[0].size(),
      range_boundaries[range_boundaries.size() - 1].data(),
      range_boundaries[range_boundaries.size() - 1].size());
  }

  ifs.close();
  fprintf(stderr, "%lu Hot ranges restored from %s\n",HotRanges->hot_ranges_.size(), file_path.c_str());
  return Status::OK();
}


Status DBImpl::RestoreMemPartitionsFromFile(const std::string& dbname_1) {
  // 使用 dbname 来构建文件路径
  std::string file_path = dbname_1 + "/MetaPartition";

  // 打开文件进行读取
  std::ifstream ifs(file_path);
  if (!ifs.is_open()) {
    // fprintf(stderr,"Error opening file for restoring mem partitions:: %s !\n", file_path.c_str());
    return Status::IOError("Error opening file for restoring mem partitions");
  }

    // 解析 JSON 数据
  nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);  // 解析时不抛出异常
  if (root.is_discarded()) {
    fprintf(stderr, "Error parsing JSON from %s: invalid JSON format.\n", file_path.c_str());
    return Status::Corruption("Error parsing JSON: invalid format");
  }

  mem_partitions_.clear();  // 清空现有的 mem_partitions_

  // 遍历 JSON 数据恢复分区
  for (const auto& partition_json : root["partitions"]) {
    mem_partition_guard* partition = new mem_partition_guard();

    // 恢复 partition 的基本信息
    partition->partition_num = partition_json["partition_num"].get<uint64_t>();
    partition->partition_start_str = partition_json["partition_start"].get<std::string>();
    partition->partition_end_str = partition_json["partition_end"].get<std::string>();
    partition->partition_start = Slice(partition->partition_start_str);
    partition->partition_end = Slice(partition->partition_end_str);
    partition->written_kvs = partition_json["written_kvs"].get<uint64_t>();
    partition->total_file_size = partition_json["total_file_size"].get<uint64_t>();
    partition->min_file_size = partition_json["min_file_size"].get<uint64_t>();
    partition->total_files = partition_json["total_files"].get<uint64_t>();
    partition->is_true_end = partition_json["is_true_end"].get<bool>();

    // 恢复 sub_partitions
    for (const auto& sub_partition_json : partition_json["sub_partitions"]) {
      mem_partition_guard* sub_partition = new mem_partition_guard();
      sub_partition->partition_num = sub_partition_json["partition_num"].get<uint64_t>();
      sub_partition->partition_start_str = sub_partition_json["partition_start"].get<std::string>();
      sub_partition->partition_end_str = sub_partition_json["partition_end"].get<std::string>();
      sub_partition->partition_start = Slice(sub_partition->partition_start_str);
      sub_partition->partition_end = Slice(sub_partition->partition_end_str);
      sub_partition->written_kvs = sub_partition_json["written_kvs"].get<uint64_t>();
      sub_partition->total_file_size = sub_partition_json["total_file_size"].get<uint64_t>();
      sub_partition->min_file_size = sub_partition_json["min_file_size"].get<uint64_t>();
      sub_partition->total_files = sub_partition_json["total_files"].get<uint64_t>();
      sub_partition->is_true_end = sub_partition_json["is_true_end"].get<bool>();

      // 插入子分区到主分区
      partition->sub_partitions_.insert(sub_partition);
    }

    // 插入主分区到 mem_partitions_
    mem_partitions_.insert(partition);
  }

  ifs.close();
  PrintPartitions(mem_partitions_);
  fprintf(stderr, "%lu Mem partitions restored from %s\n",mem_partitions_.size(), file_path.c_str());
  return Status::OK();
}


void DBImpl::SaveMemPartitionsToFile() {

  std::string file_path = dbname_ + "/MetaPartition";

  // 打开文件以二进制模式写入
  std::ofstream ofs(file_path, std::ios::binary);
  if (!ofs) {
    fprintf(stderr,"Error opening file for saving mem partitions: %s! \n", file_path.c_str()); 
    return;
  }

  // 写入开头的 JSON 格式
  ofs << "{\n";
  ofs << "  \"partitions\": [\n";

    bool first_partition = true;
    for (const auto& partition_guard : mem_partitions_) {
        if (!first_partition) {
            ofs << ",\n";  // 在每个分区后添加逗号
        }
        first_partition = false;

        const mem_partition_guard* partition = partition_guard;

        // 写入 partition 的基本信息
        ofs << "    {\n";
        ofs << "      \"partition_num\": " << partition->partition_num << ",\n";
        ofs << "      \"partition_start\": \"" << partition->partition_start.ToString() << "\",\n";
        ofs << "      \"partition_end\": \"" << partition->partition_end.ToString() << "\",\n";
        ofs << "      \"written_kvs\": " << partition->written_kvs << ",\n";
        ofs << "      \"total_file_size\": " << partition->total_file_size << ",\n";
        ofs << "      \"min_file_size\": " << partition->min_file_size << ",\n";
        ofs << "      \"total_files\": " << partition->total_files << ",\n";
        ofs << "      \"is_true_end\": " << (partition->is_true_end ? "true" : "false") << ",\n";
        ofs << "      \"sub_partitions\": [\n";

        bool first_sub_partition = true;
        for (const auto& sub_partition_guard : partition->sub_partitions_) {
            if (!first_sub_partition) {
                ofs << ",\n";  // 在每个子分区后添加逗号
            }
            first_sub_partition = false;

            const mem_partition_guard* sub_partition = sub_partition_guard;

            // 写入 sub_partition 的信息
            // 写入 sub_partition 的详细信息
            ofs << "        {\n";
            ofs << "          \"partition_num\": " << sub_partition->partition_num << ",\n";
            ofs << "          \"partition_start\": \"" << sub_partition->partition_start.ToString() << "\",\n";
            ofs << "          \"partition_end\": \"" << sub_partition->partition_end.ToString() << "\",\n";
            ofs << "          \"written_kvs\": " << sub_partition->written_kvs << ",\n";
            ofs << "          \"total_file_size\": " << sub_partition->total_file_size << ",\n";
            ofs << "          \"min_file_size\": " << sub_partition->min_file_size << ",\n";
            ofs << "          \"total_files\": " << sub_partition->total_files << ",\n";
            ofs << "          \"is_true_end\": " << (sub_partition->is_true_end ? "true" : "false") << "\n";
            ofs << "        }";
        }

        ofs << "\n      ]\n";
        ofs << "    }";
    }

    ofs << "\n  ]\n";
    ofs << "}\n";

    ofs.close();  
}

void DBImpl::SaveHotRangesToFile() {

  // 定义保存文件路径
    std::string file_path = dbname_ + "/MetaHotRange";

    // 打开文件以二进制模式写入
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs) {
        fprintf(stderr, "Error opening file for saving hot ranges: %s\n", file_path.c_str());
        return;
    }

    // 写入开头的 JSON 格式
    ofs << "{\n";
    ofs << "  \"hot_ranges\": [\n";

    bool first_hot_range = true;
    // 遍历 hot_ranges 并写入每个区间的 start 和 end 信息
    for (const auto& hot_range : HotRanges->hot_ranges_) {
        if (!first_hot_range) {
            ofs << ",\n";  // 在每个热区间后添加逗号
        }
        first_hot_range = false;

        // 写入当前 hot_range 的 start 和 end
        ofs << "    {\n";
        ofs << "      \"start\": \"" << std::string(hot_range.start_ptr, hot_range.start_size) << "\",\n";
        ofs << "      \"end\": \"" << std::string(hot_range.end_ptr, hot_range.end_size) << "\"\n";
        ofs << "    }";
    }

    ofs << "\n  ],\n";

    // 存储 largest_intensity_range
    if (HotRanges->largest_intensity_range != nullptr) {
        ofs << "  \"largest_intensity_range\": {\n";
        ofs << "    \"start\": \"" << std::string(HotRanges->largest_intensity_range->start_ptr,
                                               HotRanges->largest_intensity_range->start_size) << "\",\n";
        ofs << "    \"end\": \"" << std::string(HotRanges->largest_intensity_range->end_ptr,
                                               HotRanges->largest_intensity_range->end_size) << "\"\n";
        ofs << "  },\n";
    }

    // 存储 min_max_range
    if (HotRanges->min_max_range != nullptr) {
        ofs << "  \"min_max_range\": {\n";
        ofs << "    \"start\": \"" << std::string(HotRanges->min_max_range->start_ptr,
                                               HotRanges->min_max_range->start_size) << "\",\n";
        ofs << "    \"end\": \"" << std::string(HotRanges->min_max_range->end_ptr,
                                               HotRanges->min_max_range->end_size) << "\"\n";
        ofs << "  }\n";
    }

    // 结束 JSON 格式
    ofs << "}\n";

    // 关闭文件
    ofs.close();
}




void DBImpl::GetApproximateSizes(const Range* range, int n, uint64_t* sizes) {
  // TODO(opt): better implementation
  MutexLock l(&mutex_);
  Version* v = versions_->current();
  v->Ref();

  for (int i = 0; i < n; i++) {
    // Convert user_key into a corresponding internal key.
    InternalKey k1(range[i].start, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey k2(range[i].limit, kMaxSequenceNumber, kValueTypeForSeek);
    uint64_t start = versions_->ApproximateOffsetOf(v, k1);
    uint64_t limit = versions_->ApproximateOffsetOf(v, k2);
    sizes[i] = (limit >= start ? limit - start : 0);
  }

  v->Unref();
}

// Default implementations of convenience methods that subclasses of DB
// can call if they wish
Status DB::Put(const WriteOptions& opt, const Slice& key, const Slice& value) {
  WriteBatch batch;
  batch.Put(key, value);
  return Write(opt, &batch);
}

Status DB::Batch_Put(const WriteOptions& opt, const Slice& key, const Slice& value) {
  return Write(opt, key, value);
}


Status DB::Delete(const WriteOptions& opt, const Slice& key) {
  WriteBatch batch;
  batch.Delete(key);
  return Write(opt, &batch);
}

DB::~DB() = default;

Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {
  *dbptr = nullptr;
  // std::fprintf(stderr, "entering db_impl.cc:%s\n", dbname.c_str());
  // fflush(stderr);

  // Initialize compaction configs before creating DBImpl instance
  CompactionConfig::InitializeCompactionConfigs();
  DBImpl* impl = new DBImpl(options, dbname);
  impl->mutex_.Lock();
  VersionEdit edit;
  // Recover handles create_if_missing, error_if_exists
  bool save_manifest = false;
  fprintf(stderr, "Before recovering in DBImpl,  save_manifest = %s\n", save_manifest ? "true" : "false");
  Status s = impl->Recover(&edit, &save_manifest);
  fprintf(stderr, "After recovering in DBImpl,  save_manifest = %s\n", save_manifest ? "true" : "false");
  
  if (s.ok() && impl->mem_ == nullptr) {
    // Create new log and a corresponding memtable.
    uint64_t new_log_number = impl->versions_->NewFileNumber();
    // fprintf(stderr, "new_log_number: %lu in DB::Open\n", new_log_number);
    WritableFile* lfile;
    s = options.env->NewWritableFile(LogFileName(dbname, new_log_number), &lfile);
    if (s.ok()) {
      edit.SetLogNumber(new_log_number);
      impl->logfile_ = lfile;
      impl->logfile_number_ = new_log_number;
      impl->log_ = new log::Writer(lfile);
      impl->mem_ = new MemTable(impl->internal_comparator_);
      impl->mem_->Ref();
    }
  }

  if (s.ok() && impl->hot_mem_ == nullptr){
    impl->hot_mem_ = new MemTable(impl->internal_comparator_);
    impl->hot_mem_->Ref();
  }
  
  
  if (s.ok() && save_manifest) {
    edit.SetPrevLogNumber(0);  // No older logs needed after recovery.
    edit.SetLogNumber(impl->logfile_number_);
    s = impl->versions_->LogAndApply(&edit, &impl->mutex_);
  }
  if (s.ok()) {
    impl->RemoveObsoleteFiles();
    impl->MaybeScheduleCompaction();
  }
  impl->mutex_.Unlock();
  if (s.ok()) {
    assert(impl->mem_ != nullptr);
    *dbptr = impl;
  } else {
    delete impl;
  }

  // //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // std::fprintf(stdout,"Test start!\n");
  // impl->batch_load_keys_from_CSV(impl->hot_file_path, impl->percentagesStr);
  // impl->initialize_level_hotcoldstats();
  // // impl->test_hot_keys();
  // std::fprintf(stdout,"Test over!\n");
  // // exit(0);
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  return s;
}

Snapshot::~Snapshot() = default;

Status DestroyDB(const std::string& dbname, const Options& options) {
  Env* env = options.env;
  std::vector<std::string> filenames;
  Status result = env->GetChildren(dbname, &filenames);
  if (!result.ok()) {
    // Ignore error in case directory does not exist
    return Status::OK();
  }

  FileLock* lock;
  const std::string lockname = LockFileName(dbname);
  result = env->LockFile(lockname, &lock);
  if (result.ok()) {
    uint64_t number;
    FileType type;
    for (size_t i = 0; i < filenames.size(); i++) {
      if (ParseFileName(filenames[i], &number, &type) &&
          type != kDBLockFile) {  // Lock file will be deleted at end
        Status del = env->RemoveFile(dbname + "/" + filenames[i]);
        if (result.ok() && !del.ok()) {
          result = del;
        }
      }
    }
    env->UnlockFile(lock);  // Ignore error since state is already gone
    env->RemoveFile(lockname);
    env->RemoveDir(dbname);  // Ignore error in case dir contains other files
  }
  return result;
}

}  // namespace leveldb
