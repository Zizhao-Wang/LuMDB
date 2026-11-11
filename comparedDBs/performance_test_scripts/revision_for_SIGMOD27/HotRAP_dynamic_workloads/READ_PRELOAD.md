# HotRAP 读实验的独立预加载副本

脚本：`hotrap_script_zipf_preload_for_reads_open1000.sh`。

默认为 Zipf 1.2、1.3、1.4 各建立 3 份独立数据库，共 9 份。每份使用同一分布 CSV 的前 10 亿条数据执行 PUT；各份不是接着前一份写，也不把 10 亿条重复写入同一个库。Zipf 中有重复 key，因此 10 亿次写入不等于 10 亿个不同 key。

每份使用固定 seed=1、16 B key、128 B value，加载相同的逻辑数据，作为后续读实验的备用副本。数据库、PM/NVM 数据目录、RALT 和日志默认带 `iter01`～`iter03` 标识，相互独立。

## 执行方法

在本目录运行：

```bash
# 默认：三个分布，每个 3 份，每份 10 亿次写入。
sudo bash hotrap_script_zipf_preload_for_reads_open1000.sh

# 只预加载 Zipf 1.3，仍然默认生成 3 份。
sudo bash hotrap_script_zipf_preload_for_reads_open1000.sh 1.3

# 指定生成份数和起始编号，例如 1.2 的 iter01～iter03。
sudo env ITERATIONS=3 START_ITERATION=1 bash hotrap_script_zipf_preload_for_reads_open1000.sh 1.2

# 将来需要一份新的备用库，使用新编号 iter11。
sudo env ITERATIONS=1 START_ITERATION=11 bash hotrap_script_zipf_preload_for_reads_open1000.sh 1.2
```

默认输入为 `/mnt/workloads/zipf1.2_keys10.0B.csv`、`zipf1.3_keys10.0B.csv`、`zipf1.4_keys10.0B.csv`。虽然文件包含 100 亿条，本脚本只读取每个文件的前 10 亿条数据，跳过 CSV 表头。

## 容量与参数

保持 FD:SD=1:10、初始热集上限=FD 的 50%、RALT 初始上限=FD 的 15%。容量按 **10 亿次写入时**的单 workload 数据规模估算，而非此前 100 亿次或两个 workload 合计的规模。

| Zipf | 10 亿次时的 SST 规模参考 | PM 快层 FD | NVM 慢层 SD | 初始热集上限 | RALT 初始上限 | 层基数 / SST 目标 |
|---|---:|---:|---:|---:|---:|---:|
| 1.2 | 6.56 GiB | 640 MiB | 6400 MiB | 320 MiB | 96 MiB | 32 / 32 MiB |
| 1.3 | 1.65 GiB | 160 MiB | 1600 MiB | 80 MiB | 24 MiB | 8 / 8 MiB |
| 1.4 | 633.87 MiB | 64 MiB | 640 MiB | 32 MiB | 约 9.6 MiB | 4 / 4 MiB |

规模参考来自 `comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_zipf_hot_SATASSD/` 下对应 `RocksDB_10B_val128_zipf<theta>_mem64MB.log` 的第 10 亿次进度快照。SST 大小包含文件开销和旧版本，仅作为容量估计；这些容量是 HotRAP 分层目标，不是文件系统硬配额。

较小的 PM 预算同时配较小的层基数和 SST 目标；按初始层大小规划，三个分布均从 L3 开始进入慢层。三者保持 64 MiB write buffer、8 MiB block cache、open_files=1000、单前台线程、batch size=1、禁用压缩、level compaction、关闭动态层容量、TTL=0，后台 6 个 compaction 和 1 个 flush。

## 副本完成与保留

每份执行 `fillzipf,flush,waitforcompaction,stats`：

1. 完成 10 亿次 PUT，每 1 亿次输出统计，共 10 点。
2. 同步 flush 剩余 memtable，等待正常后台 compaction 完成，不执行强制全库 compact。
3. 进程正常退出、操作数和等待状态校验通过后，写入 `PRELOAD_COMPLETE`。

目录为：

```text
DB/WAL： /mnt/nvm/hotrap1B_read_preload/db/<case_name>/
PM SST： /mnt/pmem0/hotrap1B_read_preload/tier0/<case_name>/
NVM SST：/mnt/nvm/hotrap1B_read_preload/tier1/<case_name>/
RALT：   /mnt/pmem0/hotrap1B_read_preload/ralt/<case_name>/
日志：   1B_HotRAP_ReadPreload_zipf_PM_NVM_PaperRatio_open1000/<case_name>/
```

每份日志目录包含 `preload_config.env`，记录四个数据库目录、`DB_PATHS`、实际操作数、seed 和容量/层/缓存等参数。后续读实验要使用对应副本的参数，并设置 `--use_existing_db=true`，保留多路径配置，不能只复制 DB 控制目录。

已有 `PRELOAD_COMPLETE` 且目录存在的副本会跳过，不重复生成。已有但未完成的目录会报错停止，不自动删除或覆盖，防止误伤预加载库。需要新的备用库时使用新的迭代编号；完成标记表示曾成功预加载，不替代使用后的数据完整性检查。

本脚本仅准备数据库，不运行正式读 workload 或 YCSB，也不修改此前独立写入/动态切换实验。
