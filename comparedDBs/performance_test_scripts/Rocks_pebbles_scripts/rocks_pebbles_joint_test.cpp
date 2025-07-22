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

// 包含 gflags 头文件
#include <gflags/gflags.h>

// 包含 RocksDB 和 PebblesDB 的头文件
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/write_batch.h"

// --- 已修复 ---
// 去掉了 "src/"，因为 CMake 已经处理了路径映射
#include "pebblesdb/db.h"
#include "pebblesdb/options.h"
#include "pebblesdb/slice.h"
#include "pebblesdb/write_batch.h"

// --- 使用 gflags 定义命令行参数 ---

// 数据库路径
DEFINE_string(rocksdb_path, "/tmp/rocksdb_joint_test", "Path for RocksDB data directory.");
DEFINE_string(leveldb_path, "/tmp/leveldb_joint_test", "Path for LevelDB data directory.");

// 测试数据文件路径参数名
DEFINE_string(data_file, "keys_data.csv", "Path to the CSV file containing keys.");

// 写入时的配置
DEFINE_int64(num_keys, 1000000, "Total number of keys to process from the data file.");
DEFINE_int64(batch_size, 1000, "Number of entries per write batch.");
DEFINE_int32(value_size, 100, "Size of each value in bytes.");

// 区分数据的阈值
DEFINE_uint64(threshold, 500000, "Keys above this threshold go to RocksDB, others to LevelDB.");

// 随机读取测试的次数
DEFINE_int32(num_reads, 10000, "Number of random reads to perform for verification.");


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

// **新增**: 用于打印数据库统计信息的辅助函数
void PrintDBStats(rocksdb::DB* rocksdb_db, leveldb::DB* pebblesdb_db) {
    // **已修改**: 使用 GetProperty 获取 RocksDB 统计信息
    std::string rocksdb_stats_str;
    if (rocksdb_db && rocksdb_db->GetProperty("rocksdb.stats", &rocksdb_stats_str)) {
        std::cout << "\n--- RocksDB Statistics ---\n" << rocksdb_stats_str << std::endl;
    }

    // LevelDB 统计
    std::string leveldb_stats;
    if (pebblesdb_db && pebblesdb_db->GetProperty("leveldb.stats", &leveldb_stats)) {
        std::cout << "\n--- LevelDB Statistics ---\n" << leveldb_stats << std::endl;
    }
}

// --- 主函数 ---
int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 准备测试数据
    struct stat buffer;
    if (stat(FLAGS_data_file.c_str(), &buffer) != 0) {
        CreateTestCsvFile(FLAGS_data_file, FLAGS_num_keys);
    }

    // 1. 初始化并打开两个数据库
    rocksdb::DB* rocksdb_db;
    rocksdb::Options rocksdb_options;
    rocksdb_options.create_if_missing = true;
    rocksdb::Status rocksdb_status = rocksdb::DB::Open(rocksdb_options, FLAGS_rocksdb_path, &rocksdb_db);
    assert(rocksdb_status.ok());
    std::cout << "RocksDB opened successfully at " << FLAGS_rocksdb_path << std::endl;

    leveldb::DB* pebblesdb_db;
    leveldb::Options leveldb_options;
    leveldb_options.create_if_missing = true;
    leveldb::Status pebblesdb_status = leveldb::DB::Open(leveldb_options, FLAGS_leveldb_path, &pebblesdb_db);
    assert(pebblesdb_status.ok());
    std::cout << "LevelDB opened successfully at " << FLAGS_leveldb_path << std::endl;

    // 2. **已修改**: 先将所有 key 从文件加载到内存中的 vector
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

    // **已修复**: 跳过第一行 (标题行)
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

    // **新增**: 在写入前暂停并提示用户清除缓存
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


    // 3. **已修改**: 从内存中的 vector 读取数据并写入数据库
    std::cout << "\n--- Starting Write Phase (from memory) ---" << std::endl;
    std::cout << "Keys > " << FLAGS_threshold << " -> RocksDB" << std::endl;
    std::cout << "Keys <= " << FLAGS_threshold << " -> LevelDB" << std::endl;

    auto write_start_time = std::chrono::high_resolution_clock::now();

    rocksdb::WriteBatch rocksdb_batch;
    leveldb::WriteBatch pebbles_batch;
    rocksdb::WriteOptions rocksdb_write_options;
    leveldb::WriteOptions pebblesdb_write_options;

    int64_t rocksdb_written_count = 0;
    int64_t pebblesdb_written_count = 0;
    int64_t pebbles_batch_count = 0; // **新增**: PebblesDB 批次计数器

    for (size_t i = 0; i < all_keys.size(); ++i) {
        const uint64_t k = all_keys[i];
        char key_str[17];
        snprintf(key_str, sizeof(key_str), "%016llu", (unsigned long long)k);
        
        std::string value = GenerateRandomString(FLAGS_value_size);

        if (k > FLAGS_threshold) {
            rocksdb_batch.Put(key_str, value);
            rocksdb_written_count++;
        } else {
            pebbles_batch.Put(leveldb::Slice(key_str), leveldb::Slice(value));
            pebblesdb_written_count++;
            pebbles_batch_count++; // **新增**: 增加计数
        }

        // 当任一批次达到大小，或处理到最后一个 key 时，提交写入
        if (rocksdb_batch.Count() >= (size_t)FLAGS_batch_size || 
            pebbles_batch_count >= (size_t)FLAGS_batch_size ||
            i == all_keys.size() - 1) {
            
            if (rocksdb_batch.Count() > 0) {
                rocksdb_status = rocksdb_db->Write(rocksdb_write_options, &rocksdb_batch);
                assert(rocksdb_status.ok());
                rocksdb_batch.Clear();
            }
            if (pebbles_batch_count > 0) {
                pebblesdb_status = pebblesdb_db->Write(pebblesdb_write_options, &pebbles_batch);
                assert(pebblesdb_status.ok());
                pebbles_batch.Clear();
                pebbles_batch_count = 0; // **新增**: 重置计数器
            }
        }
    }

    auto write_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_duration = write_end_time - write_start_time;
    
    // **已修改**: 在写操作后立即打印性能和统计数据
    std::cout << "\n--- Write Phase Report ---" << std::endl;
    std::cout << "Total keys written: " << all_keys.size() << std::endl;
    std::cout << "  - To RocksDB: " << rocksdb_written_count << std::endl;
    std::cout << "  - To LevelDB: " << pebblesdb_written_count << std::endl;
    std::cout << "Total write time: " << write_duration.count() << " seconds" << std::endl;
    std::cout << "Write OPS: " << (double)all_keys.size() / write_duration.count() << " keys/sec" << std::endl;
    
    std::cout << "\n--- DB STATS AFTER WRITE PHASE ---";
    PrintDBStats(rocksdb_db, pebblesdb_db);

    // **已修改**: 在读取前暂停并提示用户清除缓存
    std::cout << "\n\n********************************************************" << std::endl;
    std::cout << "  Load phase complete. Clearing file system cache..." << std::endl;
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
    std::cout << "  Continuing to write phase..." << std::endl;
    std::cout << "********************************************************\n" << std::endl;

    // 4. 读取阶段
    std::cout << "\n\n--- Starting Random Read Phase ---" << std::endl;
    auto read_start_time = std::chrono::high_resolution_clock::now();

    std::mt19937 gen(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<size_t> distrib(0, all_keys.size() - 1);
    rocksdb::ReadOptions rocksdb_read_options;
    leveldb::ReadOptions leveldb_read_options;
    int successful_reads = 0;
    int failed_reads = 0;

    for (int i = 0; i < FLAGS_num_reads; ++i) {
        uint64_t k = all_keys[distrib(gen)];
        char key_str[17];
        snprintf(key_str, sizeof(key_str), "%016llu", (unsigned long long)k);
        std::string value_read;
        
        if (k > FLAGS_threshold) {
            rocksdb_status = rocksdb_db->Get(rocksdb_read_options, key_str, &value_read);
            if (rocksdb_status.ok()) successful_reads++; else failed_reads++;
        } else {
            pebblesdb_status = pebblesdb_db->Get(leveldb_read_options, key_str, &value_read);
            if (pebblesdb_status.ok()) successful_reads++; else failed_reads++;
        }
    }

    auto read_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_duration = read_end_time - read_start_time;

    // **已修改**: 在读操作后打印性能和最终统计数据
    std::cout << "\n--- Read Phase Report ---" << std::endl;
    std::cout << "Successful reads: " << successful_reads << std::endl;
    std::cout << "Failed reads: " << failed_reads << std::endl;
    std::cout << "Total read time: " << read_duration.count() << " seconds" << std::endl;
    std::cout << "Read OPS: " << (double)FLAGS_num_reads / read_duration.count() << " reads/sec" << std::endl;
    
    std::cout << "\n--- FINAL DB STATS AFTER READ PHASE ---";
    PrintDBStats(rocksdb_db, pebblesdb_db);

    // 5. 清理
    std::cout << "\n\n--- Cleaning up ---" << std::endl;
    delete rocksdb_db;
    delete pebblesdb_db;

    return 0;
}

