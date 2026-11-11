# HotRAP 单负载写入实验

这组实验分别运行 Zipf 1.1、Zipf 1.2、cluster 40、cluster 49。每个 workload 都有自己的数据库、PM 快层、NVM 慢层、RALT 和日志目录，并启动独立的 `db_bench` 进程。前一个进程结束后才开始下一个，不把多个文件传入同一个数据库。

在本目录执行：

```bash
# 依次完成两个独立的 Zipf 实验，各写入 100 亿次。
sudo bash hotrap_script_zipf_write_independent_open1000.sh

# 依次完成两个独立的 cluster 实验，各写入 1 亿次。
sudo bash HotRAP_cluster_write_independent_open1000.sh

# 也可以一次只跑一个，或自行指定执行顺序。
sudo bash hotrap_script_zipf_write_independent_open1000.sh 1.2
sudo bash HotRAP_cluster_write_independent_open1000.sh 49
sudo bash HotRAP_cluster_write_independent_open1000.sh 49 40
```

每个实验只传入一个输入文件，使用 `fillzipf,stats` 或 `fillcluster,stats`，不执行后续 YCSB。这里“完整写入”指完成上述操作数，不是持续写到设备没有可用空间。输入文件仍为 `/mnt/workloads/zipf1.1_keys10.0B.csv`、`zipf1.2_keys10.0B.csv`、`second_cluster40.sort`、`second_cluster49.sort`。

## 容量依据和参数

容量按各个 workload 单独从空库写入后的 SST 规模估算，预留取整余量；不按两文件合计规模或累计用户写入量配置。SST 大小包含文件开销、尚未清理的旧版本，因此是规模参考，不是精确有效数据量。

| Workload | 单负载 SST 参考 | PM 快层 FD | NVM 慢层 SD | 初始热集上限 | RALT 初始上限 | L1 容量基数 | SST 目标大小 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Zipf 1.1 | 210.48 GiB | 20 GiB | 200 GiB | 10 GiB | 3 GiB | 256 MiB | 64 MiB |
| Zipf 1.2 | 43.91 GiB | 4352 MiB（4.25 GiB） | 42.5 GiB | 2176 MiB | 约 652.8 MiB | 256 MiB | 64 MiB |
| cluster 40 | 14.08 GiB | 1344 MiB | 13440 MiB | 672 MiB | 约 201.6 MiB | 64 MiB | 64 MiB |
| cluster 49 | 2.12 GiB | 208 MiB | 2080 MiB | 104 MiB | 约 31.2 MiB | 8 MiB | 8 MiB |

所有实验保持 FD:SD=1:10，初始热集上限为 FD 的 50%，RALT 初始物理大小上限为 FD 的 15%。这些数值是目标预算，不是文件系统配额。PM 上是 tier0 和 RALT；NVM 上是 tier1 以及 DB 控制文件/WAL。

规模参考来自：

- Zipf：`comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_SATASSD/RocksDB_10B_val128_zipf1.1_mem64MB.log` 和同目录的 `RocksDB_10B_val128_zipf1.2_mem64MB.log`，均为单文件 100 亿次写入后的最终 SST。
- cluster：`comparedDBs/performance_test_scripts/revision_for_SIGMOD27/RocksDB_dynamic_workloads/10B_10B_RocksDB_NVM_TwitterCluster_workloads_open1000/` 下 `RocksDB_1B_val1038_mem64MB_cluster40_49.log`、`RocksDB_1B_val1038_mem64MB_cluster49_40.log` 的第一个 workload 结束时统计，各自是从空库开始的 1 亿次写入，不采用第二阶段或最终两文件的总量。

HotRAP 的初始层分配按 L0/L1 基数、默认 10 倍层倍率和 FD 预算决定快慢层边界，再由 AutoTuner 调整快层倍率。较小的数据集需要更小的层基数；上述配置的初始慢层均从 L3 开始。cluster 49 的 PM 很小，SST 目标也相应缩小为 8 MiB，避免 64 MiB 文件相对层预算过大。它不再与其他 workload 共用相同的 SST 参数；跨系统对比时需要记录这个差异。

其他参数保持：单个前台线程、batch size=1、64 MiB write buffer、8 MiB block cache、open_files=1000、禁用压缩、level compaction、关闭动态层容量、TTL=0；后台为 6 个 compaction 和 1 个 flush。

## 输出与重跑

每个 workload 输出 10 个进度统计点、单文件吞吐/延迟直方图和数据库 stats，以及最终汇总。Zipf 每 10 亿次输出一次，cluster 每 1000 万次输出一次。独立实验的累计计数在每个新进程中从零开始。

日志根目录为 `10B_HotRAP_Independent_zipf_PM_NVM_PaperRatio_open1000/` 和 `HotRAP_Independent_TwitterCluster_PM_NVM_PaperRatio_open1000/`，每个 workload 的子目录包含容量、层基数和 SST 参数。数据库目录使用 `single_zipf...` 或 `single_cluster...` 前缀，与原动态实验分开。

日志已存在时跳过该实验。需要重跑时先归档对应日志子目录；脚本会在启动该 workload 前重建它自己的数据库目录，不清理其他 workload 的库。原动态切换脚本和日志保留。
