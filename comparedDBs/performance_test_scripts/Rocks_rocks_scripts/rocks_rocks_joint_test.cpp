#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cassert>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>
#include <ctime>   // **新增**: 用于获取和格式化时间
#include <iomanip> // **新增**: 用于 std::put_time

// 包含 gflags 头文件
#include <gflags/gflags.h>

// 包含 RocksDB 头文件
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/table.h"
#include "rocksdb/cache.h"

// --- 使用 gflags 定义命令行参数 ---

// 数据库路径
DEFINE_string(rocksdb1_path, "/tmp/rocksdb1_test", "Path for first RocksDB data directory.");
DEFINE_string(rocksdb2_path, "/tmp/rocksdb2_test", "Path for second RocksDB data directory.");

// 测试数据文件路径参数名
DEFINE_string(data_file, "keys_data.csv", "Path to the CSV file containing keys.");

// 写入时的配置
DEFINE_int64(num_keys, 1000000, "Total number of keys to process from the data file.");
DEFINE_int64(batch_size, 1000, "Number of entries per write batch.");
DEFINE_int32(value_size, 100, "Size of each value in bytes.");

// 区分数据的阈值
DEFINE_uint64(threshold, 500000, "Keys above this threshold go to RocksDB1, others to RocksDB2.");

// 随机读取测试的次数
DEFINE_int32(num_reads, 10000, "Number of random reads to perform for verification.");

// RocksDB1 配置选项
DEFINE_uint64(rocksdb1_memtable_size, 64 * 1024 * 1024, "RocksDB1 memtable size in bytes (default: 64MB).");
DEFINE_int32(rocksdb1_level0_file_num_compaction_trigger, 4, "RocksDB1 level0 file num compaction trigger.");
DEFINE_int32(rocksdb1_level0_slowdown_writes_trigger, 20, "RocksDB1 level0 slowdown writes trigger.");
DEFINE_int32(rocksdb1_level0_stop_writes_trigger, 36, "RocksDB1 level0 stop writes trigger.");
DEFINE_uint64(rocksdb1_max_bytes_for_level_base, 256 * 1024 * 1024, "RocksDB1 max bytes for level base (default: 256MB).");
DEFINE_double(rocksdb1_max_bytes_for_level_multiplier, 10.0, "RocksDB1 max bytes for level multiplier.");
DEFINE_uint64(rocksdb1_block_cache_size, 128 * 1024 * 1024, "RocksDB1 block cache size in bytes (default: 128MB).");
DEFINE_int32(rocksdb1_max_background_jobs, 8, "RocksDB1 max background jobs.");

// RocksDB2 配置选项
DEFINE_uint64(rocksdb2_memtable_size, 64 * 1024 * 1024, "RocksDB2 memtable size in bytes (default: 64MB).");
DEFINE_int32(rocksdb2_level0_file_num_compaction_trigger, 4, "RocksDB2 level0 file num compaction trigger.");
DEFINE_int32(rocksdb2_level0_slowdown_writes_trigger, 20, "RocksDB2 level0 slowdown writes trigger.");
DEFINE_int32(rocksdb2_level0_stop_writes_trigger, 36, "RocksDB2 level0 stop writes trigger.");
DEFINE_uint64(rocksdb2_max_bytes_for_level_base, 256 * 1024 * 1024, "RocksDB2 max bytes for level base (default: 256MB).");
DEFINE_double(rocksdb2_max_bytes_for_level_multiplier, 10.0, "RocksDB2 max bytes for level multiplier.");
DEFINE_uint64(rocksdb2_block_cache_size, 128 * 1024 * 1024, "RocksDB2 block cache size in bytes (default: 128MB).");
DEFINE_int32(rocksdb2_max_background_jobs, 8, "RocksDB2 max background jobs.");

// --- 辅助函数 ---

std::string GenerateRandomString(size_t length) {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    std::string random_string;
    random_string.reserve(length);
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<size_t> distribution(0, max_index - 1);
    for (size_t i = 0; i < length; ++i) {
        random_string += charset[distribution(generator)];
    }
    return random_string;
}

void CreateTestCsvFile(const std::string& filename, int64_t num_keys) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create CSV file: " << filename << std::endl;
        exit(1);
    }
    std::cout << "Generating " << num_keys << " keys into " << filename << "..." << std::endl;
    std::vector<uint64_t> keys(num_keys);
    for (int64_t i = 0; i < num_keys; ++i) {
        keys[i] = i + 1;
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(keys.begin(), keys.end(), g);

    for (const auto& key : keys) {
        file << key << "\n";
    }
    file.close();
    std::cout << "CSV file generated successfully." << std::endl;
}

// 配置RocksDB选项的辅助函数
rocksdb::Options CreateRocksDBOptions(bool is_rocksdb1) {
    rocksdb::Options options;
    options.create_if_missing = true;
    
    if (is_rocksdb1) {
        // RocksDB1 配置
        options.write_buffer_size = FLAGS_rocksdb1_memtable_size;
        options.level0_file_num_compaction_trigger = FLAGS_rocksdb1_level0_file_num_compaction_trigger;
        options.level0_slowdown_writes_trigger = FLAGS_rocksdb1_level0_slowdown_writes_trigger;
        options.level0_stop_writes_trigger = FLAGS_rocksdb1_level0_stop_writes_trigger;
        options.max_bytes_for_level_base = FLAGS_rocksdb1_max_bytes_for_level_base;
        options.max_bytes_for_level_multiplier = FLAGS_rocksdb1_max_bytes_for_level_multiplier;
        options.max_background_jobs = FLAGS_rocksdb1_max_background_jobs;
        
        // 设置块缓存
        rocksdb::BlockBasedTableOptions table_options;
        table_options.block_cache = rocksdb::NewLRUCache(FLAGS_rocksdb1_block_cache_size);
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        
        std::cout << "RocksDB1 Configuration:" << std::endl;
        std::cout << "  - Memtable size: " << FLAGS_rocksdb1_memtable_size / (1024 * 1024) << " MB" << std::endl;
        std::cout << "  - Level0 compaction trigger: " << FLAGS_rocksdb1_level0_file_num_compaction_trigger << std::endl;
        std::cout << "  - Level multiplier: " << FLAGS_rocksdb1_max_bytes_for_level_multiplier << std::endl;
        std::cout << "  - Block cache size: " << FLAGS_rocksdb1_block_cache_size / (1024 * 1024) << " MB" << std::endl;
        std::cout << "  - Max background jobs: " << FLAGS_rocksdb1_max_background_jobs << std::endl;
        
    } else {
        // RocksDB2 配置
        options.write_buffer_size = FLAGS_rocksdb2_memtable_size;
        options.level0_file_num_compaction_trigger = FLAGS_rocksdb2_level0_file_num_compaction_trigger;
        options.level0_slowdown_writes_trigger = FLAGS_rocksdb2_level0_slowdown_writes_trigger;
        options.level0_stop_writes_trigger = FLAGS_rocksdb2_level0_stop_writes_trigger;
        options.max_bytes_for_level_base = FLAGS_rocksdb2_max_bytes_for_level_base;
        options.max_bytes_for_level_multiplier = FLAGS_rocksdb2_max_bytes_for_level_multiplier;
        options.max_background_jobs = FLAGS_rocksdb2_max_background_jobs;
        
        // 设置块缓存
        rocksdb::BlockBasedTableOptions table_options;
        table_options.block_cache = rocksdb::NewLRUCache(FLAGS_rocksdb2_block_cache_size);
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        
        std::cout << "RocksDB2 Configuration:" << std::endl;
        std::cout << "  - Memtable size: " << FLAGS_rocksdb2_memtable_size / (1024 * 1024) << " MB" << std::endl;
        std::cout << "  - Level0 compaction trigger: " << FLAGS_rocksdb2_level0_file_num_compaction_trigger << std::endl;
        std::cout << "  - Level multiplier: " << FLAGS_rocksdb2_max_bytes_for_level_multiplier << std::endl;
        std::cout << "  - Block cache size: " << FLAGS_rocksdb2_block_cache_size / (1024 * 1024) << " MB" << std::endl;
        std::cout << "  - Max background jobs: " << FLAGS_rocksdb2_max_background_jobs << std::endl;
    }
    
    return options;
}

// 用于打印数据库统计信息的辅助函数
void PrintDBStats(rocksdb::DB* rocksdb1_db, rocksdb::DB* rocksdb2_db) {
    // RocksDB1 统计信息
    std::string rocksdb1_stats_str;
    if (rocksdb1_db && rocksdb1_db->GetProperty("rocksdb.stats", &rocksdb1_stats_str)) {
        std::cout << "\n--- RocksDB1 Statistics ---\n" << rocksdb1_stats_str << std::endl;
    }

    // RocksDB2 统计信息
    std::string rocksdb2_stats_str;
    if (rocksdb2_db && rocksdb2_db->GetProperty("rocksdb.stats", &rocksdb2_stats_str)) {
        std::cout << "\n--- RocksDB2 Statistics ---\n" << rocksdb2_stats_str << std::endl;
    }
}

// **新增**: 打印当前时间的辅助函数
void PrintCurrentTime(const std::string& phase_name) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::cout << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] "
              << phase_name << std::endl;
}

// --- 主函数 ---
int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    PrintCurrentTime("Benchmark Started");

    // 打印测试配置
    std::cout << "\n=== Test Configuration ===" << std::endl;
    std::cout << "Number of keys: " << FLAGS_num_keys << std::endl;
    std::cout << "Value size: " << FLAGS_value_size << " bytes" << std::endl;
    std::cout << "Batch size: " << FLAGS_batch_size << std::endl;
    std::cout << "Threshold: " << FLAGS_threshold << std::endl;
    std::cout << "Number of reads: " << FLAGS_num_reads << std::endl;
    std::cout << "Data file: " << FLAGS_data_file << std::endl;
    std::cout << "RocksDB1 path: " << FLAGS_rocksdb1_path << std::endl;
    std::cout << "RocksDB2 path: " << FLAGS_rocksdb2_path << std::endl;
    std::cout << "==============================\n" << std::endl;

    // 准备测试数据
    struct stat buffer;
    if (stat(FLAGS_data_file.c_str(), &buffer) != 0) {
        CreateTestCsvFile(FLAGS_data_file, FLAGS_num_keys);
    }

    // 1. 初始化并打开两个RocksDB数据库
    rocksdb::DB* rocksdb1_db;
    rocksdb::Options rocksdb1_options = CreateRocksDBOptions(true);
    rocksdb::Status rocksdb1_status = rocksdb::DB::Open(rocksdb1_options, FLAGS_rocksdb1_path, &rocksdb1_db);
    assert(rocksdb1_status.ok());
    std::cout << "RocksDB1 opened successfully at " << FLAGS_rocksdb1_path << std::endl;

    rocksdb::DB* rocksdb2_db;
    rocksdb::Options rocksdb2_options = CreateRocksDBOptions(false);
    rocksdb::Status rocksdb2_status = rocksdb::DB::Open(rocksdb2_options, FLAGS_rocksdb2_path, &rocksdb2_db);
    assert(rocksdb2_status.ok());
    std::cout << "RocksDB2 opened successfully at " << FLAGS_rocksdb2_path << std::endl;

    // 2. 先将所有 key 从文件加载到内存中的 vector
    std::cout << "\n--- Starting Load Phase ---" << std::endl;
    std::vector<uint64_t> all_keys;
    all_keys.reserve(FLAGS_num_keys);
    
    std::ifstream csv_file(FLAGS_data_file);
    if (!csv_file.is_open()) {
        fprintf(stderr, "Unable to open CSV file: %s\n", FLAGS_data_file.c_str());
        return 1;
    }
    
    std::string line;
    std::stringstream line_stream;
    int64_t line_number = 0; // 行号计数器

    // 跳过第一行 (标题行)
    if (std::getline(csv_file, line)) {
        line_number++;
        std::cout << "Skipping header row (line " << line_number << "): " << line << std::endl;
    }

    while (all_keys.size() < (size_t)FLAGS_num_keys && std::getline(csv_file, line)) {
        line_number++; // 每读取一行，行号加一
        std::vector<std::string> row_data;
        std::string cell;
        
        line_stream.clear();
        line_stream.str(line);

        while (std::getline(line_stream, cell, ',')) {
            row_data.push_back(cell);
        }

        if (row_data.size() != 1) {
            // 在错误信息中加入行号
            fprintf(stderr, "Error on line %lld: Invalid CSV row format: expected 1 column but got %zu in line: %s\n", (long long)line_number, row_data.size(), line.c_str());
            continue;
        }

        try {
            all_keys.push_back(std::stoull(row_data[0]));
        } catch (const std::invalid_argument& ia) {
            // 在错误信息中加入行号
            fprintf(stderr, "Error on line %lld: Invalid argument in CSV (not a number): %s\n", (long long)line_number, row_data[0].c_str());
        } catch (const std::out_of_range& oor) {
            // 在错误信息中加入行号
            fprintf(stderr, "Error on line %lld: Out of range in CSV (number too large): %s\n", (long long)line_number, row_data[0].c_str());
        }
    }
    csv_file.close();
    std::cout << "Loaded " << all_keys.size() << " keys into memory." << std::endl;

    // 在写入前清除文件系统缓存
    std::cout << "\n\n********************************************************" << std::endl;
    std::cout << "  Load phase complete. Clearing file system cache..." << std::endl;
    std::cout << "********************************************************" << std::endl;
    // 先同步文件系统，确保所有待写入数据都写入磁盘
    sync();
    // 清除所有缓存（页面缓存 + 目录项和inode缓存）
    int result = system("echo 3 > /proc/sys/vm/drop_caches");
    if (result == 0) {
        std::cout << "  File system cache cleared successfully." << std::endl;
    } else {
        std::cout << "  Failed to clear cache. Error code: " << result << std::endl;
    }
    std::cout << "  Continuing to write phase..." << std::endl;
    std::cout << "********************************************************\n" << std::endl;

    // 3. 从内存中的 vector 读取数据并写入两个RocksDB数据库
    std::cout << "\n--- Starting Write Phase (from memory) ---" << std::endl;
    std::cout << "Keys > " << FLAGS_threshold << " -> RocksDB1" << std::endl;
    std::cout << "Keys <= " << FLAGS_threshold << " -> RocksDB2" << std::endl;

    auto write_start_time = std::chrono::high_resolution_clock::now();

    rocksdb::WriteBatch rocksdb1_batch;
    rocksdb::WriteBatch rocksdb2_batch;
    rocksdb::WriteOptions rocksdb1_write_options;
    rocksdb::WriteOptions rocksdb2_write_options;

    int64_t rocksdb1_written_count = 0;
    int64_t rocksdb2_written_count = 0;

    for (size_t i = 0; i < all_keys.size(); ++i) {
        const uint64_t k = all_keys[i];
        char key_str[17];
        snprintf(key_str, sizeof(key_str), "%016llu", (unsigned long long)k);
        
        std::string value = GenerateRandomString(FLAGS_value_size);

        if (k > FLAGS_threshold) {
            rocksdb1_batch.Put(key_str, value);
            rocksdb1_written_count++;
        } else {
            rocksdb2_batch.Put(key_str, value);
            rocksdb2_written_count++;
        }

        // 当任一批次达到大小，或处理到最后一个 key 时，提交写入
        if (rocksdb1_batch.Count() >= (size_t)FLAGS_batch_size || 
            rocksdb2_batch.Count() >= (size_t)FLAGS_batch_size ||
            i == all_keys.size() - 1) {
            
            if (rocksdb1_batch.Count() > 0) {
                rocksdb1_status = rocksdb1_db->Write(rocksdb1_write_options, &rocksdb1_batch);
                assert(rocksdb1_status.ok());
                rocksdb1_batch.Clear();
            }
            if (rocksdb2_batch.Count() > 0) {
                rocksdb2_status = rocksdb2_db->Write(rocksdb2_write_options, &rocksdb2_batch);
                assert(rocksdb2_status.ok());
                rocksdb2_batch.Clear();
            }
        }
    }

    auto write_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_duration = write_end_time - write_start_time;
    
    // 在写操作后立即打印性能和统计数据
    std::cout << "\n--- Write Phase Report ---" << std::endl;
    std::cout << "Total keys written: " << all_keys.size() << std::endl;
    std::cout << "  - To RocksDB1: " << rocksdb1_written_count << std::endl;
    std::cout << "  - To RocksDB2: " << rocksdb2_written_count << std::endl;
    std::cout << "Total write time: " << write_duration.count() << " seconds" << std::endl;
    std::cout << "Write OPS: " << (double)all_keys.size() / write_duration.count() << " keys/sec" << std::endl;
    
    std::cout << "\n--- DB STATS AFTER WRITE PHASE ---";
    PrintDBStats(rocksdb1_db, rocksdb2_db);

    // 在读取前清除文件系统缓存
    std::cout << "\n\n********************************************************" << std::endl;
    std::cout << "  Write phase complete. Clearing file system cache..." << std::endl;
    std::cout << "********************************************************" << std::endl;
    // 先同步文件系统，确保所有待写入数据都写入磁盘
    sync();
    // 清除所有缓存（页面缓存 + 目录项和inode缓存）
    int result2 = system("echo 3 > /proc/sys/vm/drop_caches");
    if (result2 == 0) {
        std::cout << "  File system cache cleared successfully." << std::endl;
    } else {
        std::cout << "  Failed to clear cache. Error code: " << result2 << std::endl;
    }
    std::cout << "  Continuing to read phase..." << std::endl;
    std::cout << "********************************************************\n" << std::endl;

    // 4. 读取阶段
    std::cout << "\n\n--- Starting Random Read Phase ---" << std::endl;
    auto read_start_time = std::chrono::high_resolution_clock::now();

    std::mt19937 gen(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<size_t> distrib(0, all_keys.size() - 1);
    rocksdb::ReadOptions rocksdb1_read_options;
    rocksdb::ReadOptions rocksdb2_read_options;
    int successful_reads = 0;
    int failed_reads = 0;
    int rocksdb1_reads = 0;
    int rocksdb2_reads = 0;

    for (int i = 0; i < FLAGS_num_reads; ++i) {
        uint64_t k = all_keys[distrib(gen)];
        char key_str[17];
        snprintf(key_str, sizeof(key_str), "%016llu", (unsigned long long)k);
        std::string value_read;
        
        if (k > FLAGS_threshold) {
            rocksdb1_status = rocksdb1_db->Get(rocksdb1_read_options, key_str, &value_read);
            rocksdb1_reads++;
            if (rocksdb1_status.ok()) successful_reads++; else failed_reads++;
        } else {
            rocksdb2_status = rocksdb2_db->Get(rocksdb2_read_options, key_str, &value_read);
            rocksdb2_reads++;
            if (rocksdb2_status.ok()) successful_reads++; else failed_reads++;
        }
    }

    auto read_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_duration = read_end_time - read_start_time;

    // 在读操作后打印性能和最终统计数据
    std::cout << "\n--- Read Phase Report ---" << std::endl;
    std::cout << "Successful reads: " << successful_reads << std::endl;
    std::cout << "Failed reads: " << failed_reads << std::endl;
    std::cout << "  - RocksDB1 reads: " << rocksdb1_reads << std::endl;
    std::cout << "  - RocksDB2 reads: " << rocksdb2_reads << std::endl;
    std::cout << "Total read time: " << read_duration.count() << " seconds" << std::endl;
    std::cout << "Read OPS: " << (double)FLAGS_num_reads / read_duration.count() << " reads/sec" << std::endl;
    
    std::cout << "\n--- FINAL DB STATS AFTER READ PHASE ---";
    PrintDBStats(rocksdb1_db, rocksdb2_db);

    // 5. 清理
    std::cout << "\n\n--- Cleaning up ---" << std::endl;
    delete rocksdb1_db;
    delete rocksdb2_db;

    PrintCurrentTime("Benchmark Finished");

    return 0;
}