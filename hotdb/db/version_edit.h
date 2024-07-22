// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_VERSION_EDIT_H_
#define STORAGE_LEVELDB_DB_VERSION_EDIT_H_

#include <set>
#include <utility>
#include <vector>

#include "db/dbformat.h"
#include "util/logging.h"
#include "leveldb/env.h"

namespace leveldb {

class VersionSet;

struct FileMetaData {
  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) {}

  int refs;
  int allowed_seeks;  // Seeks allowed until compaction
  uint64_t number;
  uint64_t file_size;    // File size in bytes
  InternalKey smallest;  // Smallest internal key served by table
  InternalKey largest;   // Largest internal key served by table
};


struct Logica_File_MetaData {
  Logica_File_MetaData() : refs(0), run_number(0),file_size(0),number(0) {
    actual_files.clear();
  }

  int refs;
  int allowed_seeks;  // Seeks allowed until compaction

  uint64_t number;
  uint64_t run_number;
  
  uint64_t file_size;
  InternalKey smallest;       // Smallest internal key served by table
  InternalKey largest;        // Largest internal key served by table

  std::vector<FileMetaData> actual_files;

  void append_physical_file(FileMetaData &f);
};

class VersionEdit {
 public:
  VersionEdit() { Clear(); }
  ~VersionEdit() = default;

  void Clear();

  void SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
  }
  void SetLogNumber(uint64_t num) {
    has_log_number_ = true;
    log_number_ = num;
  }
  void SetPrevLogNumber(uint64_t num) {
    has_prev_log_number_ = true;
    prev_log_number_ = num;
  }
  void SetNextFile(uint64_t num) {
    has_next_file_number_ = true;
    next_file_number_ = num;
  }
  void SetLastSequence(SequenceNumber seq) {
    has_last_sequence_ = true;
    last_sequence_ = seq;
  }
  void SetCompactPointer(int level, uint64_t partition_num, const InternalKey& key) {
    compact_pointers_.push_back(std::make_tuple(level, partition_num, key));
  }

  void SetCompactPointer(int level, int run, const InternalKey& key) {
    tier_compact_pointers_.push_back(std::make_tuple(level, run, key));
  }

  void set_is_tiering() {
    is_tiering_edit=true;
  }

  bool get_is_tiering() const {
    return is_tiering_edit;
  }

  void set_logger(Logger* logger) {
    logger_ = logger;
  }

  Logger* get_logger() const {
    return logger_;
  }


  // Add the specified file at the specified number.
  // REQUIRES: This version has not been saved (see VersionSet::SaveTo)
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file
  void AddFile(int level, uint64_t file, uint64_t file_size,
               const InternalKey& smallest, const InternalKey& largest) {
    FileMetaData f;
    f.number = file;
    f.file_size = file_size;
    f.smallest = smallest;
    f.largest = largest;

    // Log(logger_, "Adding file to %s files: #%llu, size: %llu, level: %d",
    //   is_tiering ? "tiering" : "leveling", (unsigned long long)file, (unsigned long long)file_size, level);
    // Log(logger_, "File smallest key: %s, largest key: %s",
      // smallest.DebugString().c_str(), largest.DebugString().c_str());
    new_files_.push_back(std::make_pair(level, f));
  }

  void AddPartitionLevelingFile(uint64_t partitioin, int level, uint64_t file, uint64_t file_size,
               const InternalKey& smallest, const InternalKey& largest) {
    FileMetaData f;
    f.number = file;
    f.file_size = file_size;
    f.smallest = smallest;
    f.largest = largest;

    // Log(logger_, "Adding file to %s files: #%llu, size: %llu, level: %d",
    //   is_tiering ? "tiering" : "leveling", (unsigned long long)file, (unsigned long long)file_size, level);
    // Log(logger_, "File smallest key: %s, largest key: %s",
      // smallest.DebugString().c_str(), largest.DebugString().c_str());
    new_partitioning_files_.push_back(std::make_tuple(level,partitioin, f));
  }


  void AddTieringFile(int level, int run, uint64_t file, uint64_t file_size,
               const InternalKey& smallest, const InternalKey& largest) {
    FileMetaData f;
    f.number = file;
    f.file_size = file_size;
    f.smallest = smallest;
    f.largest = largest;

    new_tiering_files.push_back(std::make_tuple(level,run,f));
    
  }


  // Delete the specified "file" from the specified "level".
  void RemoveFile(int level, uint64_t file) {
    deleted_files_.insert(std::make_pair(level, file));
  }

  void RemovePartitionFile(int level,uint64_t partition, uint64_t file) {
    deleted_partitioning_files_.insert(std::make_tuple(level,partition, file));
  }

  void RemovetieringFile(int level, int run, uint64_t file) {
    deleted_tiering_files_.insert(std::make_tuple(level, run, file));
  }

  void EncodeTo(std::string* dst) const;
  Status DecodeFrom(const Slice& src);

  std::string DebugString() const;

 private:
  friend class VersionSet;

  typedef std::set<std::pair<int, uint64_t>> DeletedFileSet;
  
  typedef std::tuple<int, int, uint64_t> DeletedTieringFileTuple;
  typedef std::set<DeletedTieringFileTuple> DeletedTieringFileSet;

  typedef std::tuple<int, uint64_t, uint64_t> DeletedPartitionLevelingFileTuple;
  typedef std::set<DeletedPartitionLevelingFileTuple> DeletedPartitionLevelFileSet;

  std::string comparator_;
  uint64_t log_number_;
  uint64_t prev_log_number_;
  uint64_t next_file_number_;
  SequenceNumber last_sequence_;
  bool has_comparator_;
  bool has_log_number_;
  bool has_prev_log_number_;
  bool has_next_file_number_;
  bool has_last_sequence_;
  Logger* logger_;

  // std::vector<std::pair<int, InternalKey>> compact_pointers_;

  std::vector<std::tuple<int, uint64_t, InternalKey>> compact_pointers_;

  std::vector<std::tuple<int, uint64_t, InternalKey>> tier_compact_pointers_;

  DeletedFileSet deleted_files_;


  // ==== Start of modified code ====



  std::vector<std::pair<int, FileMetaData>> new_files_;

  std::vector<std::tuple<int,uint64_t, FileMetaData>> new_partitioning_files_;
  DeletedPartitionLevelFileSet deleted_partitioning_files_;

  std::vector<std::tuple<int, int, FileMetaData>> new_tiering_files;
  DeletedTieringFileSet deleted_tiering_files_;

  bool is_tiering_edit;
  // ==== End of modified code ====
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_EDIT_H_
