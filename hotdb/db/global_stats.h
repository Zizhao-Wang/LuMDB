// global_stats.h
#ifndef GLOBAL_STATS_H
#define GLOBAL_STATS_H

#include <atomic>

namespace leveldb {

// 在 table.cc 文件顶部定义
struct InternalGetStats {
    int64_t index_block_seek_time;
    int64_t filter_check_time;
    int64_t block_load_seek_time;
    int64_t taotal_time;

    InternalGetStats() : index_block_seek_time(0), filter_check_time(0),
        block_load_seek_time(0),taotal_time(0){}

    void Reset() {
        index_block_seek_time = 0;
        filter_check_time = 0;
        block_load_seek_time = 0;
        taotal_time = 0;
    }
};

struct GlobalGetStats {
  std::atomic<int64_t> index_block_seek_time;
  std::atomic<int64_t> filter_check_time;
  std::atomic<int64_t> block_load_seek_time;
  std::atomic<int64_t> taotal_time;

  GlobalGetStats() : index_block_seek_time(0), filter_check_time(0), block_load_seek_time(0),taotal_time(0) {}

  void Add(const InternalGetStats& stats) {
    index_block_seek_time.fetch_add(stats.index_block_seek_time, std::memory_order_relaxed);
    filter_check_time.fetch_add(stats.filter_check_time, std::memory_order_relaxed);
    block_load_seek_time.fetch_add(stats.block_load_seek_time, std::memory_order_relaxed);
    taotal_time.fetch_add(stats.taotal_time, std::memory_order_relaxed);
  }
};

extern GlobalGetStats global_stats;

} // namespace leveldb

#endif // GLOBAL_STATS_H
