// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_edit.h"

#include "db/version_set.h"
#include "util/coding.h"

namespace leveldb {

// Tag numbers for serialized VersionEdit.  These numbers are written to
// disk and should not be changed.
// enum Tag {
//   kComparator = 1,
//   kLogNumber = 2,
//   kNextFileNumber = 3,
//   kLastSequence = 4,
//   kCompactPointer = 5,
//   kDeletedFile = 6,
//   kNewFile = 7,
//   // 8 was used for large value refs
//   kPrevLogNumber = 9
// };

enum Tag {
  kComparator = 1,
  kLogNumber = 2,
  kNextFileNumber = 3,
  kLastSequence = 4,
  kCompactPointer = 5,
  kDeletedLFile = 6,
  kNewLFile = 7,
  // 8 was used for large value refs
  kPrevLogNumber = 9,
  kDeletedTFile = 10,
  kNewTFile = 11
};

void VersionEdit::Clear() {
  comparator_.clear();
  log_number_ = 0;
  prev_log_number_ = 0;
  last_sequence_ = 0;
  next_file_number_ = 0;
  has_comparator_ = false;
  has_log_number_ = false;
  has_prev_log_number_ = false;
  has_next_file_number_ = false;
  has_last_sequence_ = false;
  is_tiering_edit = false;
  compact_pointers_.clear();
  deleted_files_.clear();
  new_files_.clear();
  new_partitioning_files_.clear();
  deleted_partitioning_files_.clear();
  new_tiering_files.clear();
  deleted_tiering_files_.clear();
}

void Logica_File_MetaData::append_physical_file(FileMetaData &f) {
  if (actual_files.size() == 0) {
    smallest = f.smallest;
    largest = f.largest;
    actual_files.push_back(f);
    return ;
  }

  if(smallest.user_key().compare(f.smallest.user_key()) > 0) {
    smallest = f.smallest;
  }

  if(largest.user_key().compare(f.largest.user_key()) < 0) {
    largest = f.largest;
  }

  file_size += f.file_size;
  actual_files.push_back(f);
}


// void VersionEdit::EncodeTo(std::string* dst) const {
//   if (has_comparator_) {
//     PutVarint32(dst, kComparator);
//     PutLengthPrefixedSlice(dst, comparator_);
//   }
//   if (has_log_number_) {
//     PutVarint32(dst, kLogNumber);
//     PutVarint64(dst, log_number_);
//   }
//   if (has_prev_log_number_) {
//     PutVarint32(dst, kPrevLogNumber);
//     PutVarint64(dst, prev_log_number_);
//   }
//   if (has_next_file_number_) {
//     PutVarint32(dst, kNextFileNumber);
//     PutVarint64(dst, next_file_number_);
//   }
//   if (has_last_sequence_) {
//     PutVarint32(dst, kLastSequence);
//     PutVarint64(dst, last_sequence_);
//   }

//   for (size_t i = 0; i < compact_pointers_.size(); i++) {
//     PutVarint32(dst, kCompactPointer);
//     PutVarint32(dst, std::get<0>(compact_pointers_[i]));  // level
//     PutVarint64(dst, std::get<1>(compact_pointers_[i]));  // partition_number
//     PutLengthPrefixedSlice(dst, std::get<2>(compact_pointers_[i]).Encode());
//   }

//   for (const auto& deleted_file_kvp : deleted_files_) {
//     PutVarint32(dst, kDeletedFile);
//     PutVarint32(dst, deleted_file_kvp.first);   // level
//     PutVarint64(dst, deleted_file_kvp.second);  // file number
//   }

//   for (size_t i = 0; i < new_files_.size(); i++) {
//     const FileMetaData& f = new_files_[i].second;
//     PutVarint32(dst, kNewFile);
//     PutVarint32(dst, new_files_[i].first);  // level
//     PutVarint64(dst, f.number);
//     PutVarint64(dst, f.file_size);
//     PutLengthPrefixedSlice(dst, f.smallest.Encode());
//     PutLengthPrefixedSlice(dst, f.largest.Encode());
//   }
// }


void VersionEdit::EncodeTo(std::string* dst) const {
  if (has_comparator_) {
    PutVarint32(dst, kComparator);
    PutLengthPrefixedSlice(dst, comparator_);
  }
  if (has_log_number_) {
    PutVarint32(dst, kLogNumber);
    PutVarint64(dst, log_number_);
  }
  if (has_prev_log_number_) {
    PutVarint32(dst, kPrevLogNumber);
    PutVarint64(dst, prev_log_number_);
  }
  if (has_next_file_number_) {
    PutVarint32(dst, kNextFileNumber);
    PutVarint64(dst, next_file_number_);
  }
  if (has_last_sequence_) {
    PutVarint32(dst, kLastSequence);
    PutVarint64(dst, last_sequence_);
  }

  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    PutVarint32(dst, kCompactPointer);
    PutVarint32(dst, std::get<0>(compact_pointers_[i]));  // level
    PutVarint64(dst, std::get<1>(compact_pointers_[i]));  // partition_number
    PutLengthPrefixedSlice(dst, std::get<2>(compact_pointers_[i]).Encode());
  }

  // for (const auto& deleted_file_kvp : deleted_files_) {
  //   PutVarint32(dst, kDeletedFile);
  //   PutVarint32(dst, deleted_file_kvp.first);   // level
  //   PutVarint64(dst, deleted_file_kvp.second);  // file number
  // }

  // for (size_t i = 0; i < new_files_.size(); i++) {
  //   const FileMetaData& f = new_files_[i].second;
  //   PutVarint32(dst, kNewFile);
  //   PutVarint32(dst, new_files_[i].first);  // level
  //   PutVarint64(dst, f.number);
  //   PutVarint64(dst, f.file_size);
  //   PutLengthPrefixedSlice(dst, f.smallest.Encode());
  //   PutLengthPrefixedSlice(dst, f.largest.Encode());
  // }


  // 编码新分区文件
  for (size_t i = 0; i < new_partitioning_files_.size(); i++) {
    const auto& file_tuple = new_partitioning_files_[i];
    const FileMetaData& f = std::get<2>(file_tuple);  // 获取 FileMetaData

    // 编码操作
    PutVarint32(dst, kNewLFile);
    PutVarint32(dst, std::get<0>(file_tuple));  // level
    PutVarint64(dst, std::get<1>(file_tuple));  // partition number
    PutVarint64(dst, f.number);                 // file number
    PutVarint64(dst, f.file_size);              // file size
    PutLengthPrefixedSlice(dst, f.smallest.Encode());  // smallest key
    PutLengthPrefixedSlice(dst, f.largest.Encode());   // largest key
  }

  // 编码删除的分区文件
  for (const auto& deleted_file_kvp : deleted_partitioning_files_) {
    PutVarint32(dst, kDeletedLFile);
    PutVarint32(dst, std::get<0>(deleted_file_kvp));  // level
    PutVarint64(dst, std::get<1>(deleted_file_kvp));  // partition
    PutVarint64(dst, std::get<2>(deleted_file_kvp));  // file number
  }


  // 编码新分层文件
  for (size_t i = 0; i < new_tiering_files.size(); i++) {
    const auto& file_tuple = new_tiering_files[i];
    const FileMetaData& f = std::get<2>(file_tuple);  // 获取 FileMetaData

    // 编码操作
    PutVarint32(dst, kNewTFile);
    PutVarint32(dst, std::get<0>(file_tuple));  // level
    PutVarint32(dst, std::get<1>(file_tuple));  // run
    PutVarint64(dst, f.number);                 // file number
    PutVarint64(dst, f.file_size);              // file size
    PutLengthPrefixedSlice(dst, f.smallest.Encode());  // smallest key
    PutLengthPrefixedSlice(dst, f.largest.Encode());   // largest key
  }

  // 编码删除的分层文件
  for (const auto& deleted_file_kvp : deleted_tiering_files_) {
    PutVarint32(dst, kDeletedTFile);
    PutVarint32(dst, std::get<0>(deleted_file_kvp));  // level
    PutVarint32(dst, std::get<1>(deleted_file_kvp));  // run
    PutVarint64(dst, std::get<2>(deleted_file_kvp));  // file number
  }

}

static bool GetInternalKey(Slice* input, InternalKey* dst) {
  Slice str;
  if (GetLengthPrefixedSlice(input, &str)) {
    return dst->DecodeFrom(str);
  } else {
    return false;
  }
}


static bool GetPartitionNumber(Slice* input, uint64_t* partition_number) {
  uint64_t v;
  if (GetVarint64(input, &v)) {
    *partition_number = v;
    return true;
  } else {
    return false;
  }
}

static bool GetLevel(Slice* input, int* level) {
  uint32_t v;
  if (GetVarint32(input, &v) && v < config::kNumLevels) {
    *level = v;
    return true;
  } else {
    return false;
  }
}

static bool GetRun(Slice* input, int* run) {
  uint32_t v;
  if (GetVarint32(input, &v) && v < config::kNum_SortedRuns) {
    *run = v;
    return true;
  } else {
    return false;
  }
}

Status VersionEdit::DecodeFrom(const Slice& src) {
  Clear();
  Slice input = src;
  const char* msg = nullptr;
  uint32_t tag;

  // Temporary storage for parsing
  int level;
  int run;
  uint64_t partition_number;
  uint64_t number;
  FileMetaData f;
  Slice str;
  InternalKey key;

  while (msg == nullptr && GetVarint32(&input, &tag)) {
    switch (tag) {
      case kComparator:
        if (GetLengthPrefixedSlice(&input, &str)) {
          comparator_ = str.ToString();
          has_comparator_ = true;
        } else {
          msg = "comparator name";
        }
        break;

      case kLogNumber:
        if (GetVarint64(&input, &log_number_)) {
          has_log_number_ = true;
        } else {
          msg = "log number";
        }
        break;

      case kPrevLogNumber:
        if (GetVarint64(&input, &prev_log_number_)) {
          has_prev_log_number_ = true;
        } else {
          msg = "previous log number";
        }
        break;

      case kNextFileNumber:
        if (GetVarint64(&input, &next_file_number_)) {
          has_next_file_number_ = true;
        } else {
          msg = "next file number";
        }
        break;

      case kLastSequence:
        if (GetVarint64(&input, &last_sequence_)) {
          has_last_sequence_ = true;
        } else {
          msg = "last sequence number";
        }
        break;

      case kCompactPointer:
        if (GetLevel(&input, &level) && GetPartitionNumber(&input, &partition_number) && GetInternalKey(&input, &key)) {
          compact_pointers_.push_back(std::make_tuple(level, partition_number,key));
        } else {
          msg = "compaction pointer";
        }
        break;
      
      // 新增的解码逻辑：new_partitioning_files_
      case kNewLFile:
        if (GetLevel(&input, &level) && GetVarint64(&input, &partition_number) &&
            GetVarint64(&input, &f.number) && GetVarint64(&input, &f.file_size) &&
            GetInternalKey(&input, &f.smallest) && GetInternalKey(&input, &f.largest)) {
          new_partitioning_files_.push_back(std::make_tuple(level, partition_number, f));
        } else {
          msg = "new-partitioning-file entry";
        }
        break;

      // 新增的解码逻辑：deleted_partitioning_files_
      case kDeletedLFile:
        if (GetLevel(&input, &level) && GetVarint64(&input, &partition_number) && GetVarint64(&input, &number)) {
          deleted_partitioning_files_.insert(std::make_tuple(level, partition_number, number));
        } else {
          msg = "deleted-partitioning-file entry";
        }
        break;

      // 新增的解码逻辑：new_tiering_files
      case kNewTFile:
        if (GetLevel(&input, &level) && GetRun(&input, &run) &&
            GetVarint64(&input, &f.number) && GetVarint64(&input, &f.file_size) &&
            GetInternalKey(&input, &f.smallest) && GetInternalKey(&input, &f.largest)) {
          new_tiering_files.push_back(std::make_tuple(level, run, f));
        } else {
          msg = "new-tiering-file entry";
        }
        break;

      // 新增的解码逻辑：deleted_tiering_files_
      case kDeletedTFile:
        if (GetLevel(&input, &level) && GetRun(&input, &run) && GetVarint64(&input, &number)) {
          deleted_tiering_files_.insert(std::make_tuple(level, run, number));
        } else {
          msg = "deleted-tiering-file entry";
        }
        break;

      default:
        msg = "unknown tag";
        break;
    }
  }

  if (msg == nullptr && !input.empty()) {
    msg = "invalid tag";
  }

  Status result;
  if (msg != nullptr) {
    result = Status::Corruption("VersionEdit", msg);
  }
  return result;
}

std::string VersionEdit::DebugString() const {
  std::string r;
  r.append("VersionEdit {");
  if (has_comparator_) {
    r.append("\n  Comparator: ");
    r.append(comparator_);
  }
  if (has_log_number_) {
    r.append("\n  LogNumber: ");
    AppendNumberTo(&r, log_number_);
  }
  if (has_prev_log_number_) {
    r.append("\n  PrevLogNumber: ");
    AppendNumberTo(&r, prev_log_number_);
  }
  if (has_next_file_number_) {
    r.append("\n  NextFile: ");
    AppendNumberTo(&r, next_file_number_);
  }
  if (has_last_sequence_) {
    r.append("\n  LastSeq: ");
    AppendNumberTo(&r, last_sequence_);
  }
  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    r.append("\n  CompactPointer: ");
    AppendNumberTo(&r, std::get<0>(compact_pointers_[i]));  // level
    r.append(" ");
    AppendNumberTo(&r, std::get<1>(compact_pointers_[i]));  // partition_number
    r.append(" ");
    r.append(std::get<2>(compact_pointers_[i]).DebugString());  // InternalKey
  }
  for (const auto& deleted_files_kvp : deleted_files_) {
    r.append("\n  RemoveFile: ");
    AppendNumberTo(&r, deleted_files_kvp.first);
    r.append(" ");
    AppendNumberTo(&r, deleted_files_kvp.second);
  }
  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    r.append("\n  AddFile: ");
    AppendNumberTo(&r, new_files_[i].first);
    r.append(" ");
    AppendNumberTo(&r, f.number);
    r.append(" ");
    AppendNumberTo(&r, f.file_size);
    r.append(" ");
    r.append(f.smallest.DebugString());
    r.append(" .. ");
    r.append(f.largest.DebugString());
  }
  r.append("\n}\n");
  return r;
}

}  // namespace leveldb
