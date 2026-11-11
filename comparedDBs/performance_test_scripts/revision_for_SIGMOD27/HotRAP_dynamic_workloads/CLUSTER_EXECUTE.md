# HotRAP 动态 cluster 混合执行

`HotRAP_cluster_execute_open1000.sh` 对照 `RocksDB_dynamic_workloads/RocksDB_cluster_execute_open1000.sh`，从空库开始，在同一个数据库、同一个进程中依次执行 **35 → 40 → 37**。每个文件执行 **100,000,000 次有效操作**，总计 **300,000,000 次**。文件之间保留数据库内容，不重新预加载、不清库、不手动 flush 或等待 compaction。

## 构建和运行

在本目录执行：

```bash
bash build_hotrap_cluster_execute.sh
DRY_RUN=1 bash HotRAP_cluster_execute_open1000.sh
sudo bash HotRAP_cluster_execute_open1000.sh

# 需要重跑时，保留旧数据，使用新的专用空目录：
sudo env RUN_ID=run02 bash HotRAP_cluster_execute_open1000.sh
```

构建生成 `share/hotrap/build-experiments/db_bench_cluster_execute_csv`；不会替换已有的预加载、点查/范围查询或 YCSB 可执行文件。原生执行代码位于 `share/hotrap/tools/db_bench_tool.cc`，入口为 `--benchmarks=twitterexecutedynamic,stats`。

## 与参考脚本一致的执行参数

| 参数 | 默认值 |
| --- | --- |
| 文件顺序 | `/mnt/workloads/second_cluster35.sort`、`second_cluster40.sort`、`second_cluster37.sort` |
| 每文件有效操作数 | 100000000 |
| `num` | 300000000 |
| 固定 value 大小 | 1038 字节 |
| key | 第 2 列，十进制 ASCII 补零至 24 字节，与参考执行代码的 `%024llu` 一致 |
| 线程 / batch | 1 / 1 |
| write buffer / target SST | 64 MiB / 64 MiB |
| block cache / Bloom bits | 8 MiB / 10 |
| open files | 1000 |
| compression | none，`compression_ratio=0` |
| 周期 stats | 每 10000000 次累计有效操作，默认 30 个点 |
| 初始状态 | 新建空库，`use_existing_db=false` |
| 清缓存 | 默认启动 db_bench 前 `sync` → `drop_caches=1` |
| 监控 | iostat 每 100 秒采样 nvme0n1 和 pmem0；perf 挂到实际 db_bench PID；内存日志 |

原参考脚本中 `MEM=512` 和外层 `10B` 标签没有决定实际写缓冲及操作数；实际使用的是循环中的 64 MiB，以及 3 × 1 亿次。本脚本按实际执行值配置。

## 混合操作语义

原始 trace 为七列、无表头，操作取第 6 列，key 取第 2 列：

- `get` / `gets`：Get，未命中也算一次有效操作；命中计入 found。
- `set` / `add` / `cas`：按参考代码执行无条件 Put，value 使用固定 1038 字节；不使用 trace 中的原 value 长度或 TTL。
- `delete`：执行 Delete，计一次有效操作。

这与 `fillcluster` 的“所有 key 都写入”流程不同。native 驱动逐行校验，未知操作、字段数不符或无效数字 key 明确计入 `skipped`，不计入有效操作数。每个文件必须完成目标有效操作数；提前 EOF 会以失败退出，不会标记完成。对每个文件最多打印前 5 个跳过记录的行号和原因，最后报告 skipped 总数。脚本在启动前抽查每个文件的前 1000 行，确认至少存在有效七列记录；不预扫或改写整个大文件。

`num` 均分到输入文件，最后一份接收余数；默认每份恰好 1 亿次。文件之间不会重开数据库，因此前一份的写入与删除对后续读取生效。

## HotRAP 的容量配置

容量使用同序列、同操作数的 RocksDB 日志作为规模基准：

```text
RocksDB_dynamic_workloads/10B_RocksDB_Dynamic_TwitterCluster_workloads_open1000/
RocksDB_execute_300000000_val1038_mem64MB_cluster35_40_37.log
```

该日志 31 个 SST 汇总快照（周期 + 最后汇总）的最大存量为 **39.24 GiB**。沿用已确定的 FD:SD = 1:10，向上取整使用 **44 GiB 总预算：PM 4 GiB、NVM 40 GiB**。预算依据 SST 存量，不使用累计写入字节数。

- tier0 和 RALT 在 `/mnt/pmem0/hotrap_cluster_execute/`；tier1、DB/WAL 在 `/mnt/nvm/hotrap_cluster_execute/`。
- 初始 hot set 上限为 FD 的 50%（2 GiB），RALT 上限为 FD 的 15%。
- level base = 256 MiB，使初始 PM 包含可调节的 L2，初始慢层从 L3 开始。
- 后台保持本组 HotRAP 实验配置：6 compaction + 1 flush，`max_background_jobs=7`。
- `compaction_style=0`、`ttl_seconds=0`、`level_compaction_dynamic_level_bytes=false`。

目录名包含 cluster 顺序、操作数、PM/SSD 容量、level base、写缓冲及 RUN_ID。已有目录不会自动删除；完成结果跳过，未完成目录要求使用新 RUN_ID，从新的空目录开始。这样不会误覆盖之前的预加载库、YCSB 工作库或动态纯写入结果。

## 统计和完成校验

日志根目录为 `HotRAP_Dynamic_TwitterCluster_execute_PM_NVM_PaperRatio_open1000`，包含完整命令、`execute_config.env`、内存、iostat 和 perf 文件。

- 全局每 1000 万次操作输出一份累计数据库 stats，默认 30 次；`[workload n/3]` 的进度计数按当前文件重新开始，文件进度不会额外触发数据库周期 stats。
- 每个文件结束额外输出该文件的吞吐、延迟直方图及 reads/writes/deletes/found/skipped，以及此时累计数据库 stats。
- 最后输出整个 `twitterexecutedynamic` 的总吞吐、直方图与计数，并执行 `stats`。
- 只有进程退出成功、总操作数正确、三个文件都完成、周期 stats 数量正确时才创建 `EXECUTE_COMPLETE`；失败保留现场。

脚本保存实际 db_bench PID，收到中断会清理查询和监控进程。`DRY_RUN=1` 只校验路径与抽样格式、打印命令，不建库、不清缓存。

可覆盖：`OPS_PER_WORKLOAD`、`STATS_INTERVAL_OPS`、`WBS_MIB`、`TIER0_MIB`、`LEVEL_BASE_MIB`、`RUN_ID`、`DROP_CACHES`、`DRY_RUN`、`PERF`、`WORKLOAD_ROOT`、`NVM_ROOT`、`PM_ROOT`、`LOG_ROOT`、`DB_BENCH`。SD 容量始终取 FD 的 10 倍。命令行可替换 cluster 顺序；更换负载或规模时，应重新依据同负载 SST 存量选择预算。

原生修改保存于完整 `hotrap_csv_bench.patch` 和增量 `hotrap_csv_cluster_execute.patch`，构建脚本自动识别尚未应用的补丁。
