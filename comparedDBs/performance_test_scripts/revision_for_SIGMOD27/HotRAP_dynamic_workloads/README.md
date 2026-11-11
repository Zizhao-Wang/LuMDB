混合执行 cluster 35 → 40 → 37 使用 `HotRAP_cluster_execute_open1000.sh`，见 [CLUSTER_EXECUTE.md](CLUSTER_EXECUTE.md)。

YCSB A–F 脚本、独立工作库与执行语义见 [YCSB.md](YCSB.md)。

点查和范围查询脚本及使用方法见 [READ_QUERIES.md](READ_QUERIES.md)。

# HotRAP 文件驱动写入实验

读实验预加载 Zipf 1.2、1.3、1.4（每份 10 亿次写入，每种默认 3 份独立备用库）使用
`hotrap_script_zipf_preload_for_reads_open1000.sh`，参数、完成标记与重开数据库方法见
[READ_PRELOAD.md](READ_PRELOAD.md)。

单独运行 Zipf 1.1、1.2、cluster 40、49（每个 workload 独立容量、空库、进程）的新脚本及参数见
[INDEPENDENT_WORKLOADS.md](INDEPENDENT_WORKLOADS.md)。
`hotrap_script_zipf_write_independent_open1000.sh` 默认依次执行两个独立 Zipf 实验，
`HotRAP_cluster_write_independent_open1000.sh` 默认依次执行两个独立 cluster 实验。
下面原有脚本仍用于同一个数据库内顺序切换负载。

本次运行下面两个独立写入脚本，结构分别参照
`LevelDB_dynamic_workloads/leveldb_script_zipf_write_open1000.sh` 和
`LevelDB_dynamic_workloads/leveldb_script_twitter_write_open1000.sh`。

```bash
cd comparedDBs/performance_test_scripts/revision_for_SIGMOD27/HotRAP_dynamic_workloads
# 尚未编译时先执行一次
bash build_hotrap.sh

# Zipf 文件写入实验
bash hotrap_script_zipf_write_open1000.sh

# cluster 文件写入实验
bash HotRAP_cluster_write_open1000.sh
```

`hotrap_script_zipf_write_open1000.sh`：

- `zipf_as=(1.3 1.1)`，依次读取 `/mnt/workloads/zipf1.3_keys10.0B.csv` 和 `/mnt/workloads/zipf1.1_keys10.0B.csv`。
- 通过 `--data_file_path="$data_file"` 把逗号分隔的文件列表交给 `--benchmarks=fillzipf,stats`。
- 每个文件 100 亿次写入，合计 200 亿次；value 128 字节。
- 同一个库按文件顺序连续写入，整个实验只在开始时清理目录。

`HotRAP_cluster_write_open1000.sh`：

- `clusters=(49 40)`，依次读取 `/mnt/workloads/second_cluster49.sort` 和 `/mnt/workloads/second_cluster40.sort`；可修改数组调整顺序。
- 使用 `--benchmarks=fillcluster,stats` 和 `--data_file_path="$data_file"`。
- `ops_per_workload=100000000`，value 1038 字节；可直接在数组里添加其他 cluster，依次写入同一个库。
- 与参考脚本的 filltwitter 写入语义一致：取第 2 列作为 key，每行执行 PUT。

两个脚本都使用 `MEM=64`、`open_files=1000`、8 MiB block cache 和禁用压缩；
write buffer 与 SST 目标大小均为 64 MiB。循环、参数、文件路径、固定日志目录、
日志存在时跳过以及 iostat/perf 监控都直接写在脚本中，参数可在脚本内修改。
`DEVICE_NAME="nvme0n1"` 沿用这次指定的 LevelDB 脚本；两个脚本同时监控 `PM_DEVICE_NAME="pmem0"`。

两个写入脚本都使用 PM 快层和 NVM 慢层：

```text
/mnt/pmem0/hotrap10B/tier0/<case_name>/
/mnt/nvm/hotrap10B/tier1/<case_name>/
```

Zipf 的数据库控制文件及 WAL 在 `/mnt/nvm/hotrap10B/db/`，RALT 元数据在
`/mnt/pmem0/hotrap10B/ralt/`。Zipf 保留论文 §4.1 的容量比例，并按本组数据规模放大：
已有 RocksDB 日志的最终 SST 总量约 213 GiB（日志显示 GB，实际按 1024 进制格式化），
作为估算基准，向上取整采用 220 GiB 总预算：快层 20 GiB、慢层 200 GiB。
初始热集上限为快层的 50%（10 GiB），RALT 物理大小初始上限为快层的 15%（3 GiB）。
SST 总量包含旧版本及文件开销，仅用于规模估算，并非精确有效数据量；容量是分层目标。
这些参数在脚本开头按比例计算。数据库子目录和日志目录带 `fd20gib_sd200gib` / `FD20GiB_SD200GiB` 标识，
与之前配置的实验分开。保留现有写入负载规模、64 MiB write buffer 和 8 MiB block cache；
这里对齐论文的容量参数，并非重现其全部实验配置。
cluster 也按相同容量比例缩放：同负载 RocksDB 的
`10B_10B_RocksDB_NVM_TwitterCluster_workloads_open1000/RocksDB_1B_val1038_mem64MB_cluster40_49.log`
记录每文件 1 亿次操作，最终 SST 约 16.08 GiB，因此取 16.5 GiB 总预算：
PM 快层 1.5 GiB、NVM 慢层 15 GiB，初始热集上限 768 MiB，RALT 初始上限约 230.4 MiB。
快层是两层总预算的 1/11；这里是按数据存量估算的实验配置，不是累计用户写入量的 1/10。
为使这个规模下 AutoTuner 能调节快层的 L2 容量，cluster 设置
`max_bytes_for_level_base=64 MiB`；write buffer 和 SST 目标仍为 64 MiB。
cluster 的数据库和日志目录带 `fd1536mib_sd15gib_lbase64mib` /
`FD1536MiB_SD15GiB_LBase64MiB` 标识，保留此前 10 GiB PM 实验的目录。
两脚本均设置 6 个后台 compaction、1 个 flush；这些配置用于当前对比，并非论文全部参数的复现。
脚本清理当前写入实验的专用目录。
HotRAP 使用 level compaction（`compaction_style=0`），设置 `ttl_seconds=0`，
并关闭动态 level bytes，以支持其热数据保留机制。

`hotrap_csv_bench.patch` 保存 HotRAP 原生 CSV benchmark 入口及构建链接修改；
构建脚本自动应用尚未应用的补丁，输出 `share/hotrap/build-experiments/db_bench`。
Zipf CSV 跳过表头；cluster CSV 无表头，保留第一条记录。

`zipf_workload_YCSB_A.sh` 已更新，并补齐 B–F 脚本，默认从 1B 预加载备用副本复制独立的 PM/NVM 工作库，详见 [YCSB.md](YCSB.md)。`hotrap_script_zipf_hot_write_before_YCSB.sh` 保留作历史预加载脚本；新 YCSB 脚本使用 `hotrap_script_zipf_preload_for_reads_open1000.sh` 生成的配置。上述两个动态写入脚本不会调用 YCSB 脚本。

统计输出与 RocksDB 多数据集实验保持相同结构：

- `stats_interval` 控制定期间隔；`stats_per_interval=1` 开启每个间隔的完整数据库 stats。Zipf 默认总操作数除以 20，即每 10 亿次输出一次。
- 运行中同时输出 `[workload 1/2]` 等当前文件进度及累计进度。
- 每个文件结束输出 `fillzipf_workload_1/2` 或 `fillcluster_workload_1/2`：该文件的操作数、耗时、吞吐、MB/s 和延迟直方图；随后输出该时间点的完整数据库 stats。
- 最后保留 `fillzipf` 或 `fillcluster` 的所有文件总汇总，以及 `stats` benchmark 的完整数据库统计。
- 文件级吞吐、延迟从各文件开始时重新计数；数据库 stats 中的 Cumulative 字段则从整个实验开始累计。文件之间不强制 flush、不等待 compaction、不重开数据库。
