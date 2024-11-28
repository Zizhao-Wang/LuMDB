// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <sys/types.h>

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "leveldb/cache.h"
#include "leveldb/comparator.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/write_batch.h"
#include "port/port.h"
#include "util/crc32c.h"
#include "util/histogram.h"
#include "util/mutexlock.h"
#include "util/random.h"
#include "util/trace.h"
#include "util/testutil.h"
#include "gflags/gflags.h"


using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//      fillseq       -- write N values in sequential key order in async mode
//      fillrandom    -- write N values in random key order in async mode
//      overwrite     -- overwrite N values in random key order in async mode
//      fillsync      -- write N/100 values in random key order in sync mode
//      fill100K      -- write N/1000 100K values in random order in async mode
//      deleteseq     -- delete N keys in sequential order
//      deleterandom  -- delete N keys in random order
//      readseq       -- read N times sequentially
//      readreverse   -- read N times in reverse order
//      readrandom    -- read N times in random order
//      readmissing   -- read N missing keys in random order
//      readhot       -- read N times in random order from 1% section of DB
//      seekrandom    -- N random seeks
//      seekordered   -- N ordered seeks
//      open          -- cost of opening a DB
//      crc32c        -- repeated crc32c of 4K of data
//   Meta operations:
//      compact     -- Compact the entire DB
//      stats       -- Print DB stats
//      sstables    -- Print sstable info
//      heapprofile -- Dump a heap profile (if supported by this port)

// static const char* FLAGS_benchmarks =
//     "fillseq,"
//     "fillsync,"
//     "fillrandom,"
//     "overwrite,"
//     "readrandom,"
//     "readrandom,"  // Extra run to allow previous compactions to quiesce
//     "readseq,"
//     "readreverse,"
//     "compact,"
//     "readrandom,"
//     "readseq,"
//     "readreverse,"
//     "fill100K,"
//     "crc32c,"
//     "snappycomp,"
//     "snappyuncomp,"
//     "zstdcomp,"
//     "zstduncomp,";


DEFINE_string(
    benchmarks,
    "fillseq,"
    "fillsync,"
    "fillrandom,"
    "overwrite,"
    "readrandom,"
    "readrandom,"  // Extra run to allow previous compactions to quiesce
    "readseq,"
    "readreverse,"
    "compact,"
    "readrandom,"
    "readseq,"
    "readreverse,"
    "fill100K,"
    "crc32c,"
    "snappycomp,"
    "snappyuncomp,"
    "zstdcomp,"
    "zstduncomp,"
    , "");

DEFINE_int32(key_prefix,0, "Common key prefix length.");

// Use the data_file with the following name.
// static const char* FLAGS_data_file = nullptr;
DEFINE_string(data_file, "", "Use the db with the following name.");

DEFINE_string(Read_data_file, "", "Use the db with the following name.");

DEFINE_string(RangeRead_data_file, "", "Use the db with the following name.");

DEFINE_string(YCSB_data_file, "", "Use the db with the following name.");

DEFINE_int64(ycsb_num, 10000000, "Number of key/values to place in database");

DEFINE_int64(No_hot_percentage, 0, "");

DEFINE_int32(zstd_compression_level, 1, "ZSTD compression level to try out");

DEFINE_string(mem_log_file,"", "log path");

// Number of key/values to place in database
DEFINE_int64(num, 1000000, "Number of key/values to place in database");
DEFINE_int64(range, 0, "key range space");

DEFINE_int64(writes, -1, "Number of write");

DEFINE_int32(prefix_length, 16,
             "Prefix length to pass into NewFixedPrefixTransform");

// Number of read operations to do.  If negative, do FLAGS_num reads.
DEFINE_int64(reads, -1, "");

DEFINE_int64(ycsb_ops_num, 1000000, "YCSB operations num");

DEFINE_int64(range_query_length, -1, "range query length");

// YCSB workload type
static leveldb::YCSBLoadType FLAGS_ycsb_type = leveldb::kYCSB_A;

// Number of concurrent threads to run.
DEFINE_int32(threads, 1, "Number of concurrent threads to run.");
DEFINE_int32(duration, 0, "Time in seconds for the random-ops tests to run."
             " When 0 then num & reads determine the test duration");

// Size of each value
DEFINE_int32(value_size, 100, "Size of each value");

DEFINE_int32(read_write_ratio, 100, "read_write_ratio");

// Arrange to generate values that shrink to this fraction of
// their original size after compression
DEFINE_double(compression_ratio, 0.5, "Arrange to generate values that shrink"
              " to this fraction of their original size after compression");

// Print histogram of operation timings
DEFINE_bool(histogram, false, "Print histogram of operation timings");
DEFINE_bool(print_wa, false, "Print write amplification every stats interval");
DEFINE_bool(comparisons, false, "Count the number of string comparisons performed");

// Number of bytes to buffer in memtable before compacting
// (initialized to default value by "main")
DEFINE_int64(write_buffer_size, 1048576,
             "Number of bytes to buffer in all memtables before compacting");

// Number of bytes written to each file.
// (initialized to default value by "main")
DEFINE_int64(max_file_size, 256 << 20, "");

// Approximate size of user data packed per block (before compression.
// (initialized to default value by "main")
DEFINE_int32(block_size, 4096,  "");
// Number of bytes to use as a cache of uncompressed data.
// Negative means use default settings.
DEFINE_int64(cache_size, 8 << 20, "");
// Maximum number of files to keep open at the same time (use default if == 0)
DEFINE_int32(open_files, 0,
             "Maximum number of files to keep open at the same time"
             " (use default if == 0)");

// Bloom filter bits per key.
// Negative means use default settings.
DEFINE_int32(bloom_bits, 10, "");

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
DEFINE_bool(use_existing_db, false, "");

// If true, reuse existing log/MANIFEST files when re-opening a database.
DEFINE_bool(reuse_logs, false, "");

// parameters of zipf distribution
DEFINE_double(zipf_dis, 1.01, "");

DEFINE_int32(seek_nexts, 50,
             "How many times to call Next() after Seek() in "
             "RangeQuery");
             
// Use the db with the following name.
// static const char* FLAGS_db = nullptr;
DEFINE_string(db, "", "Use the db with the following name.");

DEFINE_string(hot_file, "", "file for storing hot keys.");


DEFINE_string(percentages, "", "percentages to define hot keys.");

DEFINE_string(logpath, "", "");

DEFINE_int64(partition, 2000, "");

DEFINE_bool(compression, true, "");

DEFINE_bool(hugepage, false, "");

DEFINE_int64(stats_interval, 1000000, "");

DEFINE_bool(log, true, "");

DEFINE_int32(low_pool, 2, "");
DEFINE_int32(high_pool, 1, "");

DEFINE_int32(log_buffer_size, 65536, "");

DEFINE_int64(batch_size, 1, "Batch size");

DEFINE_bool(mem_append, false, "mem table use append mode");
DEFINE_bool(direct_io, false, "enable direct io");
DEFINE_bool(no_close, false, "close file after pwrite");
DEFINE_bool(skiplistrep, true, "use skiplist as memtable");
DEFINE_bool(log_dio, true, "log use direct io");
DEFINE_bool(bg_cancel, false, "allow bg compaction to be canceled");

DEFINE_int32(io_record_pid, 0, "");
DEFINE_int32(rwdelay, 10, "delay in us");
DEFINE_int32(sleep, 100, "sleep for write in readwhilewriting2");
DEFINE_int64(report_interval, 20, "report time interval");




namespace leveldb {

namespace {
leveldb::Env* g_env = nullptr;

class CountComparator : public Comparator {
 public:
  CountComparator(const Comparator* wrapped) : wrapped_(wrapped) {}
  ~CountComparator() override {}
  int Compare(const Slice& a, const Slice& b) const override {
    count_.fetch_add(1, std::memory_order_relaxed);
    return wrapped_->Compare(a, b);
  }
  const char* Name() const override { return wrapped_->Name(); }
  void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override {
    wrapped_->FindShortestSeparator(start, limit);
  }

  void FindShortSuccessor(std::string* key) const override {
    return wrapped_->FindShortSuccessor(key);
  }

  size_t comparisons() const { return count_.load(std::memory_order_relaxed); }

  void reset() { count_.store(0, std::memory_order_relaxed); }

 private:
  mutable std::atomic<size_t> count_{0};
  const Comparator* const wrapped_;
};

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(size_t len) {
    if(len>data_.size()){
      len = data_.size()-1;
    }
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }

  // Slice Generate(size_t len) {
  //   if (len > data_.size()) {
  //       len = data_.size(); 
  //   }
  //   if (pos_ + len > data_.size()) {
  //       pos_ = 0; 
  //   }
  //   pos_ += len;
  //   return Slice(data_.data() + pos_ - len, len);
  // }

};

class KeyBuffer {
 public:
  KeyBuffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, 'a', FLAGS_key_prefix);
  }
  KeyBuffer& operator=(KeyBuffer& other) = delete;
  KeyBuffer(KeyBuffer& other) = delete;

  void Set(int k) {
    std::snprintf(buffer_ + FLAGS_key_prefix,
                  sizeof(buffer_) - FLAGS_key_prefix, "%016d", k);
  }

  Slice slice() const { return Slice(buffer_, FLAGS_key_prefix + 16); }

 private:
  char buffer_[1024];
};

class variable_Buffer{
 public:
  variable_Buffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, 'a', FLAGS_key_prefix);
    this->key_sizes = 16;
  }
  variable_Buffer& operator=(variable_Buffer& other) = delete;
  variable_Buffer(variable_Buffer& other) = delete;

  void Set(uint64_t k,int key_size=16) {
    // std::snprintf(buffer_ + FLAGS_key_prefix, sizeof(buffer_) - FLAGS_key_prefix, "%016d", k);
    assert(key_size <= sizeof(buffer_) - FLAGS_key_prefix);
    char format[20];
    std::snprintf(format, sizeof(format), "%%0%dllu", key_size);
    std::snprintf(buffer_ + FLAGS_key_prefix, sizeof(buffer_) - FLAGS_key_prefix, format, (unsigned long long)k);
    this->key_sizes = key_size;
  }

  void PrintBuffer() const {
    std::cout.write(buffer_, FLAGS_key_prefix + key_sizes);
    std::cout << std::endl;
  }

  Slice slice() const { return Slice(buffer_, FLAGS_key_prefix + this->key_sizes); }

 private:
  char buffer_[1024];
  int key_sizes;
};


#if defined(__linux)
static Slice TrimSpace(Slice s) {
  size_t start = 0;
  while (start < s.size() && isspace(s[start])) {
    start++;
  }
  size_t limit = s.size();
  while (limit > start && isspace(s[limit - 1])) {
    limit--;
  }
  return Slice(s.data() + start, limit - start);
}
#endif

static void AppendWithSpace(std::string* str, Slice msg) {
  if (msg.empty()) return;
  if (!str->empty()) {
    str->push_back(' ');
  }
  str->append(msg.data(), msg.size());
}

// class Stats {
//  private:
//   double start_;
//   double finish_;
//   double seconds_;
//   uint64_t done_;
//   int next_report_;
//   int64_t bytes_;
//   double last_op_finish_;
//   uint64_t last_report_finish_;
//   Histogram hist_;
//   std::string message_;

//  public:
//   Stats() { Start(); }

//   void Start() {
//     next_report_ = 100;
//     hist_.Clear();
//     done_ = 0;
//     bytes_ = 0;
//     seconds_ = 0;
//     message_.clear();
//     start_ = finish_ = last_op_finish_ = g_env->NowMicros();
//   }

//   void Merge(const Stats& other) {
//     hist_.Merge(other.hist_);
//     done_ += other.done_;
//     bytes_ += other.bytes_;
//     seconds_ += other.seconds_;
//     if (other.start_ < start_) start_ = other.start_;
//     if (other.finish_ > finish_) finish_ = other.finish_;

//     // Just keep the messages from one thread
//     if (message_.empty()) message_ = other.message_;
//   }

//   void Stop() {
//     finish_ = g_env->NowMicros();
//     seconds_ = (finish_ - start_) * 1e-6;
//   }

//   void AddMessage(Slice msg) { AppendWithSpace(&message_, msg); }

//   void PrintSpeed() {

//     uint64_t now = Env::Default()->NowMicros();
//     int64_t usecs_since_last = now - last_report_finish_;

//     std::string cur_time = Env::Default()->TimeToString(now/1000000);
//     fprintf(stdout,
//             "%s ... thread %d: (%llu,%llu) ops and "
//             "(%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
//             cur_time.c_str(), 
//             id_,
//             done_ - last_report_done_, done_,
//             (done_ - last_report_done_) /
//             (usecs_since_last / 1000000.0),
//             done_ / ((now - start_) / 1000000.0),
//             (now - last_report_finish_) / 1000000.0,
//             (now - start_) / 1000000.0);
//     last_report_finish_ = now;
//     last_report_done_ = done_;

//     // std::string io_res = GetStdoutFromCommand("echo q| htop -u hanson | aha --line-fix | html2text -width 999 | grep -v 'F1Help' | grep -v 'xml version=' | grep kv_bench ");
//     // Log(io_log, " --- %s\n%s", cur_time.c_str(), io_res.c_str());
//     fflush(stdout);
//   }

//   void FinishedSingleOp() {
//     if (FLAGS_histogram) {
//       double now = g_env->NowMicros();
//       double micros = now - last_op_finish_;
//       hist_.Add(micros);
//       if (micros > 20000) {
//         std::fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
//         std::fflush(stderr);
//       }
//       last_op_finish_ = now;
//     }

//     done_++;
//     if (done_ >= next_report_) {
//       if (next_report_ < 1000)
//         next_report_ += 100;
//       else if (next_report_ < 5000)
//         next_report_ += 500;
//       else if (next_report_ < 10000)
//         next_report_ += 1000;
//       else if (next_report_ < 50000)
//         next_report_ += 5000;
//       else if (next_report_ < 100000)
//         next_report_ += 10000;
//       else if (next_report_ < 500000)
//         next_report_ += 50000;
//       else
//         next_report_ += 100000;

//       if((FLAGS_stats_interval != -1) && done_ % FLAGS_stats_interval == 0) {
//         PrintSpeed(); 
//           std::string stats;
//           if (!db->GetProperty("leveldb.stats", &stats)) {
//             stats = "(failed)";
//           }
//           fprintf(stdout, "\n%s\n", stats.c_str());
//       }
      

//       std::fprintf(stderr, "... finished %lu ops%30s\r", done_, "");
//       std::fflush(stderr);
//     }
//   }

//   void AddBytes(int64_t n) { bytes_ += n; }

//   void Report(const Slice& name) {
//     // Pretend at least one op was done in case we are running a benchmark
//     // that does not call FinishedSingleOp().
//     if (done_ < 1) done_ = 1;

//     std::string extra;
//     if (bytes_ > 0) {
//       // Rate is computed on actual elapsed time, not the sum of per-thread
//       // elapsed times.
//       double elapsed = (finish_ - start_) * 1e-6;
//       char rate[100];
//       std::snprintf(rate, sizeof(rate), "%6.1f MB/s",
//                     (bytes_ / 1048576.0) / elapsed);
//       extra = rate;
//     }
//     AppendWithSpace(&extra, message_);

//     std::fprintf(stdout, "%-12s : %11.3f micros/op;%s%s\n",
//                  name.ToString().c_str(), seconds_ * 1e6 / done_,
//                  (extra.empty() ? "" : " "), extra.c_str());
//     std::fprintf(stdout, "%lu operations have been finished (%.3f MB data have been written into db)\n", done_, bytes_/1048576.0);
//     if (FLAGS_histogram) {
//       std::fprintf(stdout, "Microseconds per op:\n%s\n",
//                    hist_.ToString().c_str());
//     }
//     std::fflush(stdout);
//   }
// };

// // State shared by all concurrent executions of the same benchmark.
// struct SharedState {
//   port::Mutex mu;
//   port::CondVar cv GUARDED_BY(mu);
//   int total GUARDED_BY(mu);

//   // Each thread goes through the following states:
//   //    (1) initializing
//   //    (2) waiting for others to be initialized
//   //    (3) running
//   //    (4) done

//   int num_initialized GUARDED_BY(mu);
//   int num_done GUARDED_BY(mu);
//   bool start GUARDED_BY(mu);

//   SharedState(int total)
//       : cv(&mu), total(total), num_initialized(0), num_done(0), start(false) {}
// };

// // Per-thread state for concurrent executions of the same benchmark.
// struct ThreadState {
//   int tid;      // 0..n-1 when running in n threads
//   Random rand;  // Has different seeds for different threads
//   Stats stats;
//   SharedState* shared;

//   ThreadState(int index, int seed) : tid(index), rand(seed), shared(nullptr) {}
// };

class Stats {
 public:
  int id_;
  double start_;
  double finish_;
  double seconds_;
  uint64_t done_;
  uint64_t last_report_done_;
  uint64_t last_report_finish_;
  uint64_t next_report_;
  double next_report_time_;
  int64_t   bytes_;
  int call_ref;
  uint64_t real_ops;
  double last_op_finish_;
  Histogram hist_;
  std::string message_;

 public:
  Stats() { Start(); }
  Stats(int id) { id_ = id; Start(); }
  void Start() {
    start_ = g_env->NowMicros();
    next_report_time_ = start_;
    next_report_ = 100;
    last_op_finish_ = start_;
    last_report_done_ = 0;
    last_report_finish_ = start_;
    hist_.Clear();
    done_ = 0;
    real_ops = 0;
    bytes_ = 0;
    call_ref = 0;
    seconds_ = 0;
    finish_ = start_;
    message_.clear();
  }

  void Merge(const Stats& other) {
    hist_.Merge(other.hist_);
    done_ += other.done_;
    bytes_ += other.bytes_;
    seconds_ += other.seconds_;
    if (other.start_ < start_) start_ = other.start_;
    if (other.finish_ > finish_) finish_ = other.finish_;

    // Just keep the messages from one thread
    if (message_.empty()) message_ = other.message_;
  }

  void Stop() {
    finish_ = g_env->NowMicros();
    seconds_ = (finish_ - start_) * 1e-6;
  }

  void AddMessage(Slice msg) {
    AppendWithSpace(&message_, msg);
  }

  std::string getCurrentTime() {
    char timeStr[100];
    time_t now = time(NULL);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(timeStr);
  } 

  void PrintSpeed() {

    uint64_t now = Env::Default()->NowMicros();
    int64_t usecs_since_last = now - last_report_finish_;

    // std::string cur_time = Env::Default()->TimeToString(now/1000000);
    fprintf(stdout,
            "%s ... thread %d: (%lu,%lu) ops and "
            "(%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
            getCurrentTime().c_str(), 
            id_,
            done_ - last_report_done_, done_,
            (done_ - last_report_done_) /
            (usecs_since_last / 1000000.0),
            done_ / ((now - start_) / 1000000.0),
            (now - last_report_finish_) / 1000000.0,
            (now - start_) / 1000000.0);
    last_report_finish_ = now;
    last_report_done_ = done_;

    // std::string io_res = GetStdoutFromCommand("echo q| htop -u hanson | aha --line-fix | html2text -width 999 | grep -v 'F1Help' | grep -v 'xml version=' | grep kv_bench ");
    // Log(io_log, " --- %s\n%s", cur_time.c_str(), io_res.c_str());
    fflush(stdout);
  }

  std::string format_memory(unsigned long bytes) {
    const double GIGABYTE = 1024.0 * 1024.0 * 1024.0;
    const double MEGABYTE = 1024.0 * 1024.0;
    std::ostringstream oss;

    if (bytes >= GIGABYTE) {
        oss << std::fixed << std::setprecision(2) << (bytes / GIGABYTE) << " GB";
    } else {
        oss << std::fixed << std::setprecision(2) << (bytes / MEGABYTE) << " MB";
    }

    return oss.str();
  }

  void print_mem_usage() {

    std::ofstream log_file;
    if (call_ref == 0) {
      // 如果call_ref为0，清空文件并从头开始写
      log_file.open(FLAGS_mem_log_file, std::ios::trunc);
    } else {
      // 如果call_ref不为0，以追加模式打开文件
      log_file.open(FLAGS_mem_log_file, std::ios::app);
    }

    if (!log_file.is_open()) {
      fprintf(stderr, "Failed to open the mem_log file: %s.\n",FLAGS_mem_log_file.c_str());  
      return ;
    }
    
    std::ifstream statm("/proc/self/statm");
    if (!statm) {
      fprintf(stderr, "Failed to open /proc/self/statm.\n");
      return;
    }
    unsigned long size, resident, shr_size;
    if (!(statm >> size >> resident >> shr_size)) {
      fprintf(stderr, "Failed to read memory usage from /proc/self/statm.\n");
      return;
    }
    long page_size = sysconf(_SC_PAGESIZE); // 页面大小，字节

    // 使用ostream的插入操作符来写入信息
    log_file << getCurrentTime() << " ... thread " << id_ << ": (" << (done_ - last_report_done_) << "," << done_ << ") ops have been finished!\n";
    log_file << "virtual memory used: " << format_memory(size * page_size) << ", "
             << "Resident set size: " << format_memory(resident * page_size) << ", "
             << "Shared Memory Usage: " << format_memory(shr_size * page_size) << " \n\n";    
    
    call_ref++;
  }

  void FinishedSingleOp2(DB* db = nullptr) {
    if (FLAGS_histogram) {
      double now = g_env->NowMicros();
      double micros = now - last_op_finish_;
      hist_.Add(micros);
      if (micros > 20000) {
        fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if      (next_report_ < 1000)   next_report_ += 100;
      else if (next_report_ < 5000)   next_report_ += 500;
      else if (next_report_ < 10000)  next_report_ += 1000;
      else if (next_report_ < 50000)  next_report_ += 5000;
      else if (next_report_ < 100000) next_report_ += 10000;
      else if (next_report_ < 500000) next_report_ += 50000;
      else                            next_report_ += 100000;
      // fprintf(stderr, "... finished %llu ops%30s\r", (unsigned long long)done_, "");
      // fflush(stderr);
    }

    if (g_env->NowMicros() > next_report_time_) {
        PrintSpeed(); 
        print_mem_usage();
        next_report_time_ += FLAGS_report_interval * 1000000;
        if (FLAGS_print_wa && db) {
          std::string stats;
          if (!db->GetProperty("leveldb.stats", &stats)) {
            stats = "(failed)";
          }
          fprintf(stdout, "\n%s\n", stats.c_str());
          fflush(stdout);
        }
    }
  }

  void FinishedSingleOp(DB* db = nullptr) {
    if (FLAGS_histogram) {
      double now = g_env->NowMicros();
      double micros = now - last_op_finish_;
      hist_.Add(micros);
      if (micros > 2000000) {
        fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if      (next_report_ < 1000)   next_report_ += 100;
      else if (next_report_ < 5000)   next_report_ += 500;
      else if (next_report_ < 10000)  next_report_ += 1000;
      else if (next_report_ < 50000)  next_report_ += 5000;
      else if (next_report_ < 100000) next_report_ += 10000;
      else if (next_report_ < 500000) next_report_ += 50000;
      else                            next_report_ += 100000;

      // if((FLAGS_stats_interval != -1) && real_ops % FLAGS_stats_interval == 0) {
      //   PrintSpeed(); 
      //   if (FLAGS_print_wa && db) {
      //     std::string stats;
      //     if (!db->GetProperty_with_whole_lsm("leveldb.stats", &stats)) {
      //       stats = "(failed)";
      //     }
      //     fprintf(stdout, "%s\n", stats.c_str());
      //     fprintf(stdout, "leveldb statistical: %lu operations (real operations: %lu) have been finished (user has been written %.3f MB data into db.)\n\n\n", done_, real_ops, bytes_/1048576.0);
      //     fflush(stdout);
      //   }
      // }
      fprintf(stderr, "... finished %llu ops%30s\r", (unsigned long long)done_, "");
      fflush(stderr);
    }
  }

  void AddBytes(int64_t n) {
    bytes_ += n;
  }

  void Addops(DB* db = nullptr) {
    real_ops++;
    if((FLAGS_stats_interval != -1) && real_ops % FLAGS_stats_interval == 0) {
      PrintSpeed(); 
      print_mem_usage();
      if (FLAGS_print_wa && db) {
        std::string stats;
        if (!db->GetProperty_with_whole_lsm("leveldb.stats", &stats)) {
          stats = "(failed)";
        }
        fprintf(stdout, "%s\n", stats.c_str());
        fprintf(stdout, "leveldb statistical: %lu operations (real operations: %lu) have been finished (user has been written %.3f MB data into db.)\n\n\n", done_, real_ops, bytes_/1048576.0);
        fflush(stdout);
      }
    }
  }

  void Report(const Slice& name) {
    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedSingleOp().
    if (done_ < 1) done_ = 1;

    std::string extra;
    double elapsed = (finish_ - start_) * 1e-6;
    if (bytes_ > 0) {
      // Rate is computed on actual elapsed time, not the sum of per-thread
      // elapsed times.
      
      char rate[100];
      snprintf(rate, sizeof(rate), "%6.1f MB/s",
               (bytes_ / 1048576.0) / elapsed);
      extra = rate;
    }
    AppendWithSpace(&extra, message_);

    double throughput = (double)done_/elapsed;
    std::fprintf(stdout, "%lu operations have been finished (%.3f MB data have been written into db)\n", done_, bytes_/1048576.0);
    fprintf(stdout, "%-12s : %11.3f micros/op %ld ops/sec;%s%s\n",
            name.ToString().c_str(),
            elapsed * 1e6 / done_,
            (long)throughput,
            (extra.empty() ? "" : " "),
            extra.c_str());
    if (FLAGS_histogram) {
      fprintf(stdout, "Microseconds per op:\n%s\n", hist_.ToString().c_str());
    }
    fflush(stdout);
  }
};

// State shared by all concurrent executions of the same benchmark.
struct SharedState {
  port::Mutex mu;
  port::CondVar cv GUARDED_BY(mu);
  int total GUARDED_BY(mu);

  // Each thread goes through the following states:
  //    (1) initializing
  //    (2) waiting for others to be initialized
  //    (3) running
  //    (4) done

  int num_initialized GUARDED_BY(mu);
  int num_done GUARDED_BY(mu);
  bool start GUARDED_BY(mu);

  SharedState(int total)
      : cv(&mu), total(total), num_initialized(0), num_done(0), start(false) { }
};

// Per-thread state for concurrent executions of the same benchmark.
struct ThreadState {
  int tid;             // 0..n-1 when running in n threads
  Random rand;         // Has different seeds for different threads
  Stats stats;
  SharedState* shared;
  Trace* trace;
  Trace* read_trace;
  RandomGenerator gen;
  ThreadState(int index)
      : tid(index),
        rand(1000 + index),
        stats(index) {
        trace = new TraceUniform(1000 + index * 345);
        read_trace = new TraceZipfian(1000 + index * 345);
  }
};


void Compress(
    ThreadState* thread, std::string name,
    std::function<bool(const char*, size_t, std::string*)> compress_func) {
  RandomGenerator gen;
  Slice input = gen.Generate(Options().block_size);
  int64_t bytes = 0;
  int64_t produced = 0;
  bool ok = true;
  std::string compressed;
  while (ok && bytes < 1024 * 1048576) {  // Compress 1G
    ok = compress_func(input.data(), input.size(), &compressed);
    produced += compressed.size();
    bytes += input.size();
    thread->stats.FinishedSingleOp();
  }

  if (!ok) {
    thread->stats.AddMessage("(" + name + " failure)");
  } else {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "(output: %.1f%%)",
                  (produced * 100.0) / bytes);
    thread->stats.AddMessage(buf);
    thread->stats.AddBytes(bytes);
  }
}

void Uncompress(
    ThreadState* thread, std::string name,
    std::function<bool(const char*, size_t, std::string*)> compress_func,
    std::function<bool(const char*, size_t, char*)> uncompress_func) {
  RandomGenerator gen;
  Slice input = gen.Generate(Options().block_size);
  std::string compressed;
  bool ok = compress_func(input.data(), input.size(), &compressed);
  int64_t bytes = 0;
  char* uncompressed = new char[input.size()];
  while (ok && bytes < 1024 * 1048576) {  // Compress 1G
    ok = uncompress_func(compressed.data(), compressed.size(), uncompressed);
    bytes += input.size();
    thread->stats.FinishedSingleOp();
  }
  delete[] uncompressed;

  if (!ok) {
    thread->stats.AddMessage("(" + name + " failure)");
  } else {
    thread->stats.AddBytes(bytes);
  }
}

}  // namespace

class Benchmark {
 private:
  Cache* cache_;
  const FilterPolicy* filter_policy_;
  DB* db_;
  int64_t num_;
  int value_size_;
  int entries_per_batch_;
  WriteOptions write_options_;
  int reads_;
  int heap_counter_;
  CountComparator count_comparator_;
  int total_thread_count_;

  void PrintHeader() {
    const int kKeySize = 16 + FLAGS_key_prefix;
    PrintEnvironment();
    std::fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
    std::fprintf(
        stdout, "Values:     %d bytes each (%d bytes after compression)\n",
        FLAGS_value_size,
        static_cast<int>(FLAGS_value_size * FLAGS_compression_ratio + 0.5));
    std::fprintf(stdout, "Entries:    %ld\n", num_);
    std::fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
                 ((static_cast<int64_t>(kKeySize + FLAGS_value_size) * num_) /
                  1048576.0));
    std::fprintf(
        stdout, "FileSize:   %.1f MB (estimated)\n",
        (((kKeySize + FLAGS_value_size * FLAGS_compression_ratio) * num_) /
         1048576.0));
    PrintWarnings();
    std::fprintf(stdout, "some command parameters have been selected:\n");
    std::fprintf(stdout, "  threads: %d\n", FLAGS_threads);
    std::fprintf(stdout, "  duration: %d\n", FLAGS_duration);
    std::fprintf(stdout, "  print_wa: %d\n", FLAGS_print_wa);
    std::fprintf(stdout, "  num_entries: %ld\n",FLAGS_num);
    std::fprintf(stdout, "  bechmark selected %s\n",FLAGS_benchmarks.c_str());
    std::fprintf(stdout, "  hot_file_path: %s\n", FLAGS_hot_file.c_str());
    std::fprintf(stdout, "  data_file_path: %s\n", FLAGS_data_file.c_str());
    std::fprintf(stdout, "  percentages: %s\n", FLAGS_percentages.c_str());
    std::fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    std::fprintf(
        stdout,
        "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n");
#endif
#ifndef NDEBUG
    std::fprintf(
        stdout,
        "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif

    // See if snappy is working by attempting to compress a compressible string
    const char text[] = "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy";
    std::string compressed;
    if (!port::Snappy_Compress(text, sizeof(text), &compressed)) {
      std::fprintf(stdout, "WARNING: Snappy compression is not enabled\n");
    } else if (compressed.size() >= sizeof(text)) {
      std::fprintf(stdout, "WARNING: Snappy compression is not effective\n");
    }
  }

  void PrintEnvironment() {
    std::fprintf(stderr, "LevelDB:    version %d.%d\n", kMajorVersion,
                 kMinorVersion);

#if defined(__linux)
    time_t now = time(nullptr);
    std::fprintf(stderr, "Date:       %s",
                 ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = std::fopen("/proc/cpuinfo", "r");
    if (cpuinfo != nullptr) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != nullptr) {
        const char* sep = strchr(line, ':');
        if (sep == nullptr) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      std::fclose(cpuinfo);
      std::fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      std::fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

 public:
  Benchmark()
      : cache_(FLAGS_cache_size >= 0 ? NewLRUCache(FLAGS_cache_size) : nullptr),
        filter_policy_(FLAGS_bloom_bits >= 0
                           ? NewBloomFilterPolicy(FLAGS_bloom_bits)
                           : nullptr),
        db_(nullptr),
        num_(FLAGS_num),
        value_size_(FLAGS_value_size),
        entries_per_batch_(1),
        reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
        heap_counter_(0),
        count_comparator_(BytewiseComparator()),
        total_thread_count_(0) {
    std::vector<std::string> files;
    g_env->GetChildren(FLAGS_db, &files);
    for (size_t i = 0; i < files.size(); i++) {
      if (Slice(files[i]).starts_with("heap-")) {
        g_env->RemoveFile(std::string(FLAGS_db) + "/" + files[i]);
      }
    }
    if (!FLAGS_use_existing_db) {
      DestroyDB(FLAGS_db, Options());
    }
  }

  ~Benchmark() {
    delete db_;
    delete cache_;
    delete filter_policy_;
  }

  void Run() {
    PrintHeader();
    // std::fprintf(stderr,"open dbs in Run()\n");
    // fflush(stderr);
    Open();

    const char* benchmarks = FLAGS_benchmarks.c_str();
    while (benchmarks != nullptr) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == nullptr) {
        name = benchmarks;
        benchmarks = nullptr;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      // Reset parameters that may be overridden below
      num_ = FLAGS_num;
      reads_ = (FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads);
      value_size_ = FLAGS_value_size;
      entries_per_batch_ = 1;
      write_options_ = WriteOptions();
      // fprintf(stdout, "num_ = %ld in Run()", num_);
      // fflush(stdout);

      void (Benchmark::*method)(ThreadState*) = nullptr;
      bool fresh_db = false;
      int num_threads = FLAGS_threads;

      if (name == Slice("open")) {
        method = &Benchmark::OpenBench;
        num_ /= 10000;
        if (num_ < 1) num_ = 1;
      } else if (name == Slice("fillseq")) {
        fresh_db = true;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillbatch")) {
        fresh_db = true;
        entries_per_batch_ = 1000;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillrandom")) {
        fresh_db = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fillzipf")) {
        fresh_db = true;
        method = &Benchmark::WriteZipf;
      } else if (name == Slice("fillzipf2")) {
        fresh_db = true;
        method = &Benchmark::WriteZipf2;
      } else if (name == Slice("filletc")) {
        fresh_db = true;
        method = &Benchmark::WriteRandom_from_file;
      }else if (name == Slice("overwrite")) {
        fresh_db = false;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fillsync")) {
        fresh_db = true;
        num_ /= 1000;
        write_options_.sync = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fill100K")) {
        fresh_db = true;
        num_ /= 1000;
        value_size_ = 100 * 1000;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("readseq")) {
        method = &Benchmark::ReadSequential;
      } else if (name == Slice("readreverse")) {
        method = &Benchmark::ReadReverse;
      } else if (name == Slice("readrandom")) {
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("readrange")) {
        method = &Benchmark::range_query_read;
      } else if (name == Slice("readmissing")) {
        method = &Benchmark::ReadMissing;
      } else if (name == Slice("seekrandom")) {
        method = &Benchmark::SeekRandom;
      } else if (name == Slice("seekordered")) {
        method = &Benchmark::SeekOrdered;
      } else if (name == Slice("readhot")) {
        method = &Benchmark::ReadHot;
      } else if (name == Slice("readrandomsmall")) {
        reads_ /= 1000;
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("deleteseq")) {
        method = &Benchmark::DeleteSeq;
      } else if (name == Slice("deleterandom")) {
        method = &Benchmark::DeleteRandom;
      } else if (name == Slice("readwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::ReadWhileWriting;
      } else if (name == Slice("compact")) {
        method = &Benchmark::Compact;
      } else if (name == Slice("crc32c")) {
        method = &Benchmark::Crc32c;
      } else if (name == Slice("snappycomp")) {
        method = &Benchmark::SnappyCompress;
      } else if (name == Slice("snappyuncomp")) {
        method = &Benchmark::SnappyUncompress;
      } else if (name == Slice("zstdcomp")) {
        method = &Benchmark::ZstdCompress;
      } else if (name == Slice("zstduncomp")) {
        method = &Benchmark::ZstdUncompress;
      } else if (name == Slice("heapprofile")) {
        HeapProfile();
      } else if (name == Slice("ycsba")) {
        FLAGS_ycsb_type = kYCSB_A;
        method = &Benchmark::YCSB_A;
      } else if (name == Slice("ycsbb")) {
        FLAGS_ycsb_type = kYCSB_B;
        method = &Benchmark::YCSB_A;
      } else if (name == Slice("ycsbc")) {
        FLAGS_ycsb_type = kYCSB_C;
        method = &Benchmark::YCSB_A;
      } else if (name == Slice("ycsbd")) {
        FLAGS_ycsb_type = kYCSB_D;
        method = &Benchmark::YCSB_A;
      } else if (name == Slice("ycsbe")) {
        FLAGS_ycsb_type = kYCSB_E;
        method = &Benchmark::YCSB_E;
      } else if (name == Slice("ycsbf")) {
        FLAGS_ycsb_type = kYCSB_F;
        method = &Benchmark::YCSB_F;
      }else if (name == Slice("stats")) {
        PrintStats("leveldb.stats");
      } else if (name == Slice("sstables")) {
        PrintStats("leveldb.sstables");
      } else {
        if (!name.empty()) {  // No error message for empty name
          std::fprintf(stderr, "unknown benchmark '%s'\n",
                       name.ToString().c_str());
        }
      }

      if (fresh_db) {
        if (FLAGS_use_existing_db) {
          std::fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n",
                       name.ToString().c_str());
          method = nullptr;
        } else {
          delete db_; 
          db_ = nullptr;
          DestroyDB(FLAGS_db, Options());
          fprintf(stderr, "open dbs:in fresh_db()\n");
          fflush(stderr);
          Open();
        }
      }

      if (method != nullptr) {
        RunBenchmark(num_threads, name, method);
      }
    }
  }

 private:
  struct ThreadArg {
    Benchmark* bm;
    SharedState* shared;
    ThreadState* thread;
    void (Benchmark::*method)(ThreadState*);
  };

  static void ThreadBody(void* v) {
    ThreadArg* arg = reinterpret_cast<ThreadArg*>(v);
    SharedState* shared = arg->shared;
    ThreadState* thread = arg->thread;
    {
      MutexLock l(&shared->mu);
      shared->num_initialized++;
      if (shared->num_initialized >= shared->total) {
        shared->cv.SignalAll();
      }
      while (!shared->start) {
        shared->cv.Wait();
      }
    }

    thread->stats.Start();
    (arg->bm->*(arg->method))(thread);
    thread->stats.Stop();

    {
      MutexLock l(&shared->mu);
      shared->num_done++;
      if (shared->num_done >= shared->total) {
        shared->cv.SignalAll();
      }
    }
  }

  void RunBenchmark(int n, Slice name,
                    void (Benchmark::*method)(ThreadState*)) {
    SharedState shared(n);

    ThreadArg* arg = new ThreadArg[n];
    for (int i = 0; i < n; i++) {
      arg[i].bm = this;
      arg[i].method = method;
      arg[i].shared = &shared;

      // ++total_thread_count_;
      // Seed the thread's random state deterministically based upon thread
      // creation across all benchmarks. This ensures that the seeds are unique
      // but reproducible when rerunning the same set of benchmarks.
      arg[i].thread = new ThreadState(i);
      arg[i].thread->shared = &shared;
      g_env->StartThread(ThreadBody, &arg[i]);

    }

    shared.mu.Lock();
    while (shared.num_initialized < n) {
      shared.cv.Wait();
    }

    shared.start = true;
    shared.cv.SignalAll();
    while (shared.num_done < n) {
      shared.cv.Wait();
    }
    shared.mu.Unlock();

    for (int i = 1; i < n; i++) {
      arg[0].thread->stats.Merge(arg[i].thread->stats);
    }
    arg[0].thread->stats.Report(name);
    if (FLAGS_comparisons) {
      fprintf(stdout, "Comparisons: %zu\n", count_comparator_.comparisons());
      count_comparator_.reset();
      fflush(stdout);
    }

    for (int i = 0; i < n; i++) {
      delete arg[i].thread;
    }
    delete[] arg;
  }

  void Crc32c(ThreadState* thread) {
    // Checksum about 500MB of data total
    const int size = 4096;
    const char* label = "(4K per op)";
    std::string data(size, 'x');
    int64_t bytes = 0;
    uint32_t crc = 0;
    while (bytes < 500 * 1048576) {
      crc = crc32c::Value(data.data(), size);
      thread->stats.FinishedSingleOp();
      bytes += size;
    }
    // Print so result is not dead
    std::fprintf(stderr, "... crc=0x%x\r", static_cast<unsigned int>(crc));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }

  void SnappyCompress(ThreadState* thread) {
    Compress(thread, "snappy", &port::Snappy_Compress);
  }

  void SnappyUncompress(ThreadState* thread) {
    Uncompress(thread, "snappy", &port::Snappy_Compress,
               &port::Snappy_Uncompress);
  }

  void ZstdCompress(ThreadState* thread) {
    Compress(thread, "zstd",
             [](const char* input, size_t length, std::string* output) {
               return port::Zstd_Compress(FLAGS_zstd_compression_level, input,
                                          length, output);
             });
  }

  void ZstdUncompress(ThreadState* thread) {
    Uncompress(
        thread, "zstd",
        [](const char* input, size_t length, std::string* output) {
          return port::Zstd_Compress(FLAGS_zstd_compression_level, input,
                                     length, output);
        },
        &port::Zstd_Uncompress);
  }

  void Open() {
    assert(db_ == nullptr);
    Options options;
    options.env = g_env;
    options.create_if_missing = !FLAGS_use_existing_db;
    options.block_cache = cache_;
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_file_size = FLAGS_max_file_size;
    options.block_size = FLAGS_block_size;
    if (FLAGS_comparisons) {
      options.comparator = &count_comparator_;
    }
    options.max_open_files = FLAGS_open_files;
    options.filter_policy = filter_policy_;
    options.reuse_logs = FLAGS_reuse_logs;
    options.compression =
        FLAGS_compression ? kSnappyCompression : kNoCompression;
    
    options.hot_file_path = FLAGS_hot_file;
    options.percentages = FLAGS_percentages;

    std::fprintf(stderr, "open dbs:in Open()\n");
    fflush(stderr);
    Status s = DB::Open(options, FLAGS_db, &db_);
    if (!s.ok()) {
      std::fprintf(stderr, "open error: %s\n", s.ToString().c_str());
      std::exit(1);
    }
  }

  void OpenBench(ThreadState* thread) {
    for (int i = 0; i < num_; i++) {
      delete db_;
      fprintf(stderr, "open dbs:in OpenBench()\n");
      fflush(stderr);
      Open();
      thread->stats.FinishedSingleOp();
    }
  }

  void WriteSeq(ThreadState* thread) { DoWrite(thread, true); }

  void WriteRandom(ThreadState* thread) { DoWrite(thread, false); }

  void WriteZipf(ThreadState* thread) { DoWrite_zipf2(thread, false); }


  void WriteZipf2(ThreadState* thread) { DoWrite_zipf3(thread, false); }

  void WriteRandom_from_file(ThreadState* thread) {
    DoWrite2(thread, false);
  }

  void DoWrite(ThreadState* thread, bool seq) {
    // fprintf(stdout, "DoWrite number: %ld\n",num_);
    // fflush(stdout);
    // exit(0);

    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;

    for (int64_t i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int64_t j = 0; j < entries_per_batch_; j++) {
        const int64_t k = seq ? i + j : (thread->trace->Next()% FLAGS_range);
        char key[100];
        snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
        batch.Put(key, gen.Generate(value_size_));
        bytes += value_size_ + strlen(key);
        if(thread->stats.done_ % FLAGS_stats_interval == 0){
          thread->stats.AddBytes(bytes);
          bytes = 0;
        }
        thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void DoWrite2(ThreadState* thread, bool seq) {
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    variable_Buffer key_buffer;

    std::ifstream csv_file(FLAGS_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {

        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        // const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
        if (!std::getline(csv_file, line)) { // 从文件中读取一行
            fprintf(stderr, "Error reading key from file\n");
            return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
            row_data.push_back(cell);
        }
        if (row_data.size() != 3) {
            fprintf(stderr, "Invalid CSV row format\n");
            continue;
        }
        const uint64_t k = std::stoull(row_data[0]); 
        int key_size = std::stoi(row_data[1]);
        // const int v = std::stoi(row_data[3]); 
        int val_size = std::stoi(row_data[2]);
        // std::cout << "k: " << k << ", key_size: " << key_size << ", v: " << v << ", val_size: " << val_size << std::endl;

        key_buffer.Set(k, key_size);
        // key_buffer.PrintBuffer();
      
        batch.Put(key_buffer.slice(), gen.Generate(val_size));

        bytes += val_size + key_buffer.slice().size();
        thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void DoWrite_zipf(ThreadState* thread, bool seq) {
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;

    std::ifstream csv_file(FLAGS_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
          line_stream.clear();
          line_stream.str("");
          row_data.clear();
          // const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
          if (!std::getline(csv_file, line)) { // 从文件中读取一行
              fprintf(stderr, "Error reading key from file\n");
              return;
          }
          line_stream << line;
          while (getline(line_stream, cell, ',')) {
              row_data.push_back(cell);
          }
          if (row_data.size() != 1) {
              fprintf(stderr, "Invalid CSV row format\n");
              continue;
          }
          const uint64_t k = std::stoull(row_data[0]);
          // const uint64_t k = seq ? i+j : (thread->trace->Next() % FLAGS_range);
          char key[100];
          snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
          batch.Put(key, gen.Generate(value_size_));
          bytes += value_size_ + strlen(key);
          thread->stats.FinishedSingleOp(db_); 
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void YCSB_A(ThreadState* thread) {

    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;
    variable_Buffer Read_Key;
    ReadOptions options;
    std::string value;
    int found = 0;

    std::ifstream csv_file(FLAGS_YCSB_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in YCSB_A\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    std::fprintf(stderr, "Processing %d entries in every Batch, FLAGS_ycsb_num = %lu\n", entries_per_batch_,FLAGS_ycsb_num);

    for (uint64_t i = 0; i < FLAGS_ycsb_num; i += entries_per_batch_) {
      batch.Clear();
      for (uint64_t j = 0; j < entries_per_batch_; j++) {
        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        // const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
        if (!std::getline(csv_file, line)) { // 从文件中读取一行
          fprintf(stderr, "Error reading key from file in YCSB_A\n");
          return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
          row_data.push_back(cell);
        }
        if (row_data.size() != 2) {
          fprintf(stderr, "Invalid CSV row format\n");
          continue;
        }

        if (!std::all_of(row_data[1].begin(), row_data[1].end(), ::isdigit)) {
          fprintf(stderr, "Non-numeric key encountered: %s in line %ld \n", row_data[1].c_str(),i);
          continue;
        }

        const uint64_t k = std::stoull(row_data[1]);

        if (row_data[0]=="READ"){
          // fprintf(stdout,"The key is %lu!\n",k);
          Read_Key.Set(k);
          if (db_->Get(options, Read_Key.slice(), &value).ok()) {
            found++;
          }
          bytes = (16 + FLAGS_value_size);
          thread->stats.AddBytes(bytes);
        }else if(row_data[0]=="INSERT1" || row_data[0]=="UPDATE1"){
          // const uint64_t k = seq ? i+j : (thread->trace->Next() % FLAGS_range);
          char key[100];
          snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
          s = db_->Put(write_options_, key, gen.Generate(value_size_));
          if (!s.ok()) {
            std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
            std::exit(1);
          }
          bytes += value_size_ + strlen(key);
          if(thread->stats.done_ % FLAGS_stats_interval == 0){
            thread->stats.AddBytes(bytes);
            bytes = 0;
          } 
        }else{}
        thread->stats.FinishedSingleOp();
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void YCSB_E(ThreadState* thread) {
    fprintf(stderr,"Enter YCSB_E\n");
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;
    variable_Buffer Read_Key;
    ReadOptions options;
    std::string value;
    int found = 0;
    

    int seek_lengthE=20;

    std::ifstream csv_file(FLAGS_YCSB_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in YCSB_A\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    std::fprintf(stderr, "Processing %d entries in every Batch, FLAGS_ycsb_num = %lu\n", entries_per_batch_,FLAGS_ycsb_num);

    for (uint64_t i = 0; i < FLAGS_ycsb_num; i += entries_per_batch_) {
      batch.Clear();
      for (uint64_t j = 0; j < entries_per_batch_; j++) {
        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        // const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
        if (!std::getline(csv_file, line)) { // 从文件中读取一行
          fprintf(stderr, "Error reading key from file in YCSB_A\n");
          return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
          row_data.push_back(cell);
        }
        if (row_data.size() != 2) {
          fprintf(stderr, "Invalid CSV row format\n");
          continue;
        }

        if (!std::all_of(row_data[1].begin(), row_data[1].end(), ::isdigit)) {
          fprintf(stderr, "Non-numeric key encountered: %s in line %ld \n", row_data[1].c_str(),i);
          continue;
        }

        const uint64_t k = std::stoull(row_data[1]);

        if (row_data[0]=="INSERT"){
          char key[100];
          snprintf(key, sizeof(key), "%016llu", (unsigned long long)(k+2000000));
          s = db_->Put(write_options_, key, gen.Generate(value_size_));
          if (!s.ok()) {
            std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
            std::exit(1);
          }
          bytes += value_size_ + strlen(key);
        }else if(row_data[0]=="READ"){
          Iterator* iter = db_->NewIterator(ReadOptions());
          Read_Key.Set(k);
          iter->Seek(Read_Key.slice());
          if (iter->Valid() && iter->key() == Read_Key.slice()) found++;
          while (seek_lengthE--){
            // fprintf(stdout,"Searched Key is: %s \n", iter->key().data());
            if(iter->Valid()){
              iter->Next();
            }
          }
          delete iter;
        }else{}
        thread->stats.AddBytes(bytes);
        bytes = 0;
        thread->stats.FinishedSingleOp();
        seek_lengthE = 20;
      }
    }
  }

  void YCSB_F(ThreadState* thread) {

    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;
    variable_Buffer Read_Key;
    ReadOptions options;
    std::string value;
    int found = 0;

    std::ifstream csv_file(FLAGS_YCSB_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in YCSB_A\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    std::fprintf(stderr, "Processing %d entries in every Batch, FLAGS_ycsb_num = %lu\n", entries_per_batch_,FLAGS_ycsb_num);

    for (uint64_t i = 0; i < FLAGS_ycsb_num; i += entries_per_batch_) {
      batch.Clear();
      for (uint64_t j = 0; j < entries_per_batch_; j++) {
        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        // const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
        if (!std::getline(csv_file, line)) { // 从文件中读取一行
          fprintf(stderr, "Error reading key from file in YCSB_A\n");
          return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
          row_data.push_back(cell);
        }
        if (row_data.size() != 2) {
          fprintf(stderr, "Invalid CSV row format\n");
          continue;
        }

        if (!std::all_of(row_data[1].begin(), row_data[1].end(), ::isdigit)) {
          fprintf(stderr, "Non-numeric key encountered: %s in line %ld \n", row_data[1].c_str(),i);
          continue;
        }

        const uint64_t k = std::stoull(row_data[1]);

        if (row_data[0]=="READ"){
          // fprintf(stdout,"The key is %lu!\n",k);
          Read_Key.Set(k);
          if (db_->Get(options, Read_Key.slice(), &value).ok()) {
            found++;
          }
          bytes = (16 + FLAGS_value_size);
          thread->stats.AddBytes(bytes);
        }else if(row_data[0]=="RMW"){
          // const uint64_t k = seq ? i+j : (thread->trace->Next() % FLAGS_range);
          Read_Key.Set(k);
          if (db_->Get(options, Read_Key.slice(), &value).ok()) {
            char key[100];
            snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
            s = db_->Put(write_options_, key, gen.Generate(value_size_));
            if (!s.ok()) {
              std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
              std::exit(1);
            }
            bytes += value_size_ + strlen(key);
          }
        }else{}
        thread->stats.FinishedSingleOp();
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void DoWrite_zipf2(ThreadState* thread, bool seq) {
    
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;

    std::ifstream csv_file(FLAGS_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in zipf2\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;
    assert(FLAGS_No_hot_percentage != -1);
    std::fprintf(stdout, "start benchmarking num_ = %ld entries in DoWrite_zipf2()\n", num_);
    uint64_t total_ops = 0;
    for (uint64_t i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (uint64_t j = 0; j < entries_per_batch_; j++) {
        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        if (!std::getline(csv_file, line)) { 
          fprintf(stderr, "Error reading key from file\n");
          return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
          row_data.push_back(cell);
        }
        if (row_data.size() != 1) {
          fprintf(stderr, "Invalid CSV row format\n");
          continue;
        }
        const uint64_t k = std::stoull(row_data[0]);
        thread->stats.Addops(db_);
        if(k <= FLAGS_No_hot_percentage){
          continue;
        }
        // const uint64_t k = seq ? i+j : (thread->trace->Next() % FLAGS_range);
        char key[100];
        snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
        batch.Put(key, gen.Generate(value_size_));
        bytes += value_size_ + strlen(key);
        if(thread->stats.real_ops % FLAGS_stats_interval == 0 ){
          // fprintf(stderr,"real_ops : %lu\n",thread->stats.real_ops);
          thread->stats.AddBytes(bytes);
          bytes = 0;
        }
        thread->stats.FinishedSingleOp();
        // thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }


  void DoWrite_zipf3(ThreadState* thread, bool seq) {
    
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;

    std::ifstream csv_file(FLAGS_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in zipf2\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;
    assert(FLAGS_No_hot_percentage != -1);
    std::fprintf(stdout, "start benchmarking num_ = %ld entries in DoWrite_zipf2()\n", num_);
    uint64_t total_ops = 0;
    for (uint64_t i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (uint64_t j = 0; j < entries_per_batch_; j++) {
        line_stream.clear();
        line_stream.str("");
        row_data.clear();
        if (!std::getline(csv_file, line)) { 
          fprintf(stderr, "Error reading key from file\n");
          return;
        }
        line_stream << line;
        while (getline(line_stream, cell, ',')) {
          row_data.push_back(cell);
        }
        if (row_data.size() != 1) {
          fprintf(stderr, "Invalid CSV row format\n");
          continue;
        }
        const uint64_t k = std::stoull(row_data[0]);
        thread->stats.Addops(db_);
        if(k <= FLAGS_No_hot_percentage){
          continue;
        }
        // const uint64_t k = seq ? i+j : (thread->trace->Next() % FLAGS_range);
        char key[100];
        snprintf(key, sizeof(key), "%016llu", (unsigned long long)k);
        batch.Put(key, gen.Generate(value_size_));
        bytes += value_size_ + strlen(key);
        if(thread->stats.real_ops % FLAGS_stats_interval == 0 ){
          // fprintf(stderr,"real_ops : %lu\n",thread->stats.real_ops);
          thread->stats.AddBytes(bytes);
          bytes = 0;
        }
        thread->stats.FinishedSingleOp();
        // thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }


  void range_query_read(ThreadState* thread) {

    ReadOptions options;
    std::string value;
    int found = 0;
    variable_Buffer Key;
    Iterator* iter = db_->NewIterator(options);

    std::ifstream csv_file(FLAGS_RangeRead_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in range_query_read\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    uint64_t seek_length = FLAGS_range_query_length;

    for (int i = 0; i < reads_; i++) {
      line_stream.clear();
      line_stream.str("");
      row_data.clear();
      
      if (!std::getline(csv_file, line)) { // 从文件中读取一行
        fprintf(stderr, "Error reading key from file\n");
        return;
      }
      line_stream << line;
      while (getline(line_stream, cell, ',')) {
        row_data.push_back(cell);
      }
      if (row_data.size() != 1) {
        fprintf(stderr, "Invalid CSV row format\n");
        continue;
      }
      const uint64_t k = std::stoull(row_data[0]);
      Key.Set(k);
      iter->Seek(Key.slice());
      if (iter->Valid() && iter->key() == Key.slice()) found++;
      while (seek_length--){
        // fprintf(stdout,"Searched Key is: %s \n", iter->key().data());
        iter->Next();
      }
      seek_length = FLAGS_range_query_length;
      thread->stats.FinishedSingleOp();
    }
    
    char msg[100];
    std::snprintf(msg, sizeof(msg), "(%d of %d found)", found, reads_);
    thread->stats.AddMessage(msg);
  }

  void ReadSequential(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions());
    int i = 0;
    int64_t bytes = 0;
    for (iter->SeekToFirst(); i < reads_ && iter->Valid(); iter->Next()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedSingleOp();
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadReverse(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions());
    int i = 0;
    int64_t bytes = 0;
    for (iter->SeekToLast(); i < reads_ && iter->Valid(); iter->Prev()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedSingleOp();
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadRandom(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    int found = 0;
    variable_Buffer key;

    std::ifstream csv_file(FLAGS_Read_data_file);
    std::string line;
    if (!csv_file.is_open()) {
        fprintf(stderr,"Unable to open CSV file in point_query_read\n");
        return;
    }
    std::getline(csv_file, line);
    std::stringstream line_stream;
    std::string cell;
    std::vector<std::string> row_data;

    for (int i = 0; i < FLAGS_reads; i++) {
      line_stream.clear();
      line_stream.str("");
      row_data.clear();
      
      if (!std::getline(csv_file, line)) { // 从文件中读取一行
        fprintf(stderr, "Error reading key from file\n");
        return;
      }
      line_stream << line;
      while (getline(line_stream, cell, ',')) {
        row_data.push_back(cell);
      }
      if (row_data.size() != 1) {
        fprintf(stderr, "Invalid CSV row format\n");
        continue;
      }
      const uint64_t k = std::stoull(row_data[0]);
      key.Set(k);
      if (db_->Get(options, key.slice(), &value).ok()) {
        found++;
      }
      thread->stats.FinishedSingleOp();
    }
    char msg[100];
    std::snprintf(msg, sizeof(msg), "(%d of %ld found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void ReadMissing(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      const int k = thread->rand.Uniform(FLAGS_num);
      key.Set(k);
      Slice s = Slice(key.slice().data(), key.slice().size() - 1);
      db_->Get(options, s, &value);
      thread->stats.FinishedSingleOp();
    }
  }

  void ReadHot(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    const int range = (FLAGS_num + 99) / 100;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      const int k = thread->rand.Uniform(range);
      key.Set(k);
      db_->Get(options, key.slice(), &value);
      thread->stats.FinishedSingleOp();
    }
  }

  void SeekRandom(ThreadState* thread) {
    ReadOptions options;
    int found = 0;
    KeyBuffer key;

    for (int i = 0; i < reads_; i++) {
      Iterator* iter = db_->NewIterator(options);
      const int k = thread->rand.Uniform(FLAGS_num);
      key.Set(k);
      iter->Seek(key.slice());
      if (iter->Valid() && iter->key() == key.slice()) found++;
      delete iter;
      thread->stats.FinishedSingleOp();
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "(%d of %ld found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void SeekOrdered(ThreadState* thread) {
    ReadOptions options;
    Iterator* iter = db_->NewIterator(options);
    int found = 0;
    int k = 0;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      k = (k + (thread->rand.Uniform(100))) % FLAGS_num;
      key.Set(k);
      iter->Seek(key.slice());
      if (iter->Valid() && iter->key() == key.slice()) found++;
      thread->stats.FinishedSingleOp();
    }
    delete iter;
    char msg[100];
    std::snprintf(msg, sizeof(msg), "(%d of %ld found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void DoDelete(ThreadState* thread, bool seq) {
    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    KeyBuffer key;
    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
        const int k = seq ? i + j : (thread->rand.Uniform(FLAGS_num));
        key.Set(k);
        batch.Delete(key.slice());
        thread->stats.FinishedSingleOp();
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "del error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
  }

  void DeleteSeq(ThreadState* thread) { DoDelete(thread, true); }

  void DeleteRandom(ThreadState* thread) { DoDelete(thread, false); }

  void ReadWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      ReadRandom(thread);
    } else {
      // Special thread that keeps writing until other threads are done.
      RandomGenerator gen;
      KeyBuffer key;
      while (true) {
        {
          MutexLock l(&thread->shared->mu);
          if (thread->shared->num_done + 1 >= thread->shared->num_initialized) {
            // Other threads have finished
            break;
          }
        }

        const int k = thread->rand.Uniform(FLAGS_num);
        key.Set(k);
        Status s =
            db_->Put(write_options_, key.slice(), gen.Generate(value_size_));
        if (!s.ok()) {
          std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          std::exit(1);
        }
      }

      // Do not count any of the preceding work/delay in stats.
      thread->stats.Start();
    }
  }

  void Compact(ThreadState* thread) { db_->CompactRange(nullptr, nullptr); }

  void PrintStats(const char* key) {
    std::string stats;
    if (!db_->GetProperty(key, &stats)) {
      stats = "(failed)";
    }
    std::fprintf(stdout, "\n%s\n", stats.c_str());
  }

  static void WriteToFile(void* arg, const char* buf, int n) {
    reinterpret_cast<WritableFile*>(arg)->Append(Slice(buf, n));
  }

  void HeapProfile() {
    char fname[100];
    std::snprintf(fname, sizeof(fname), "%s/heap-%04d", FLAGS_db.c_str(),
                  ++heap_counter_);
    WritableFile* file;
    Status s = g_env->NewWritableFile(fname, &file);
    if (!s.ok()) {
      std::fprintf(stderr, "%s\n", s.ToString().c_str());
      return;
    }
    bool ok = port::GetHeapProfile(WriteToFile, file);
    delete file;
    if (!ok) {
      std::fprintf(stderr, "heap profiling not supported\n");
      g_env->RemoveFile(fname);
    }
  }
};

}  // namespace leveldb

int main(int argc, char** argv) {
  FLAGS_write_buffer_size = leveldb::Options().write_buffer_size;
  FLAGS_max_file_size = leveldb::Options().max_file_size;
  FLAGS_block_size = leveldb::Options().block_size;
  FLAGS_open_files = leveldb::Options().max_open_files;
  std::string default_db_path;

  // for (int i = 1; i < argc; i++) {
  //   double d;
  //   int n;
  //   char junk;
  //   if (leveldb::Slice(argv[i]).starts_with("--benchmarks=")) {
  //     FLAGS_benchmarks = argv[i] + strlen("--benchmarks=");
  //   } else if (sscanf(argv[i], "--compression_ratio=%lf%c", &d, &junk) == 1) {
  //     FLAGS_compression_ratio = d;
  //   } else if (sscanf(argv[i], "--histogram=%d%c", &n, &junk) == 1 &&
  //              (n == 0 || n == 1)) {
  //     FLAGS_histogram = n;
  //   } else if (sscanf(argv[i], "--comparisons=%d%c", &n, &junk) == 1 &&
  //              (n == 0 || n == 1)) {
  //     FLAGS_comparisons = n;
  //   } else if (sscanf(argv[i], "--use_existing_db=%d%c", &n, &junk) == 1 &&
  //              (n == 0 || n == 1)) {
  //     FLAGS_use_existing_db = n;
  //   } else if (sscanf(argv[i], "--reuse_logs=%d%c", &n, &junk) == 1 &&
  //              (n == 0 || n == 1)) {
  //     FLAGS_reuse_logs = n;
  //   } else if (sscanf(argv[i], "--compression=%d%c", &n, &junk) == 1 &&
  //              (n == 0 || n == 1)) {
  //     FLAGS_compression = n;
  //   } else if (sscanf(argv[i], "--num=%lu%c", &n, &junk) == 1) {
  //     FLAGS_num = n;
  //   }else if (sscanf(argv[i], "--range=%lu%c", &n, &junk) == 1) {
  //     FLAGS_range = n;
  //   } else if (sscanf(argv[i], "--reads=%d%c", &n, &junk) == 1) {
  //     FLAGS_reads = n;
  //   }else if (sscanf(argv[i], "--stats_interval=%d%c", &n, &junk) == 1) {
  //     FLAGS_stats_interval = n;
  //   } else if (sscanf(argv[i], "--threads=%d%c", &n, &junk) == 1) {
  //     FLAGS_threads = n;
  //   } else if (sscanf(argv[i], "--value_size=%d%c", &n, &junk) == 1) {
  //     FLAGS_value_size = n;
  //   } else if (sscanf(argv[i], "--write_buffer_size=%d%c", &n, &junk) == 1) {
  //     FLAGS_write_buffer_size = n;
  //   } else if (sscanf(argv[i], "--max_file_size=%d%c", &n, &junk) == 1) {
  //     FLAGS_max_file_size = n;
  //   } else if (sscanf(argv[i], "--block_size=%d%c", &n, &junk) == 1) {
  //     FLAGS_block_size = n;
  //   } else if (sscanf(argv[i], "--key_prefix=%d%c", &n, &junk) == 1) {
  //     FLAGS_key_prefix = n;
  //   } else if (sscanf(argv[i], "--cache_size=%d%c", &n, &junk) == 1) {
  //     FLAGS_cache_size = n;
  //   } else if (sscanf(argv[i], "--bloom_bits=%d%c", &n, &junk) == 1) {
  //     FLAGS_bloom_bits = n;
  //   } else if (sscanf(argv[i], "--open_files=%d%c", &n, &junk) == 1) {
  //     FLAGS_open_files = n;
  //   } else if (strncmp(argv[i], "--db=", 5) == 0) {
  //     FLAGS_db = argv[i] + 5;
  //   }else if (strncmp(argv[i], "--data_file=", 12) == 0) {
  //     FLAGS_data_file = argv[i] + 12;
  //   } else {
  //     std::fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
  //     std::exit(1);
  //   }
  // }

  for (int i = 0; i < argc; ++i) {
    printf("%s ", argv[i]);
  }
  printf("\n");
  ParseCommandLineFlags(&argc, &argv, true);

  leveldb::g_env = leveldb::Env::Default();

  // Choose a location for the test database if none given with --db=<path>
  if (FLAGS_db == "") {
    leveldb::g_env->GetTestDirectory(&default_db_path);
    default_db_path += "/dbbench";
    FLAGS_db = default_db_path.c_str();
  }

  if(FLAGS_range == 0){
    FLAGS_range = FLAGS_num;
  }

  if (FLAGS_logpath == "") {
    FLAGS_logpath = FLAGS_db;
  }

  leveldb::Benchmark benchmark;
  benchmark.Run();
  return 0;
}
