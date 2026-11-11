# HotRAP：对预加载 Zipf 库执行查询

两个脚本默认依次查询 Zipf 1.2、1.3、1.4，每个执行 500,000 次查询。命令行可以只选择其中一项。每次仅重开一份已完成的预加载库，使用其 `preload_config.env` 中保存的 DB、PM tier0、NVM tier1、RALT 路径，以及原来的分层容量、level base 和后台线程配置（6 compaction + 1 flush）。查询时覆盖写缓冲和目标 SST 大小为 1 MiB，与参考查询脚本一致；不会改写已经存在的 SST 文件大小。

| 脚本 | CSV 文件 | 默认预加载副本 | 查询入口 |
| --- | --- | --- | --- |
| `hotrap_script_zipf_random_read_open1000.sh` | `/mnt/workloads/random_read_workload<theta>.csv` | iter01 | `readcsv,stats` |
| `hotrap_script_zipf_random_range_read_open1000.sh` | `/mnt/workloads/range_read_workload<theta>.csv` | iter02 | `scancsv,stats` |

副本 iter03 默认留作备用。读操作会更新 HotRAP 的热度、RALT，并可能触发 promotion，因此两类实验默认使用不同副本。重复实验请选择未使用过的副本；脚本不会复制、删除或重建原数据库。`PRELOAD_COMPLETE` 不存在时会拒绝查询，包括仍在写入的库。

## 构建和运行

在本目录执行：

```bash
bash build_hotrap_reads.sh
# 先检查恢复的路径和完整参数，不打开数据库、不创建查询日志：
DRY_RUN=1 bash hotrap_script_zipf_random_read_open1000.sh 1.2

# 点查：使用副本 1；范围查：使用副本 2。
sudo bash hotrap_script_zipf_random_read_open1000.sh
sudo bash hotrap_script_zipf_random_range_read_open1000.sh

# 只查询 Zipf 1.3，改用备用副本 3：
sudo env PRELOAD_ITERATION=3 bash hotrap_script_zipf_random_read_open1000.sh 1.3
```

`build_hotrap_reads.sh` 在现有构建目录中生成独立程序 `share/hotrap/build-experiments/db_bench_read_csv`，不替换正在预加载时使用的 `db_bench`。首次初始化构建目录仍使用 `build_hotrap.sh`。CSV 源码修改保存在 `hotrap_csv_bench.patch`；旧版 CSV checkout 的增量升级补丁为 `hotrap_csv_reads.patch`。

所有选中的预加载副本都通过检查后才开始查询。如果部分 Zipf 还没有完成预加载，可先在命令后只指定已经完成的 Zipf。

## 查询含义

点查只接受 `READ,key`。数字 key 编码与预加载相同：十进制、左侧补零至 16 字节，不会按 `num` 取模。输出实际命中数，未命中计入操作数和延迟。

范围查询只接受 `SCAN,key`。与 PebblesDB 的 `YCSB_E` 实现对齐：默认以 CSV key 加 2,000,000 作为起始 key，Seek 后读取最多 20 条，包括 Seek 定位到的第一条；到数据库末尾可以少于 20 条。每个 SCAN 计为一次操作，扫描返回的记录数另行输出为 `scanned_records`，同时输出 `nonempty_scans` 和 `exact_seek_matches`。每次查询创建并释放 iterator。

PebblesDB 在 `src/db/db_bench.cc` 的 `YCSB_E` 中固定 `seek_lengthE=20`，并使用 `k+2000000` 定位。本脚本默认 `SCAN_LENGTH=20`、`SCAN_KEY_OFFSET=2000000`，每条 SCAN 新建 iterator、Seek、读取最多 20 条后释放 iterator。参考 RocksDB 脚本没有设置 `seek_nexts`，默认值为 0；若需要复现该脚本只 Seek 的行为，可以显式设置：

```bash
sudo env SCAN_LENGTH=0 SCAN_KEY_OFFSET=2000000 \
    bash hotrap_script_zipf_random_range_read_open1000.sh 1.2
```

`QUERY_OPS` 默认 500000，与当前 CSV 的 50 万条查询一致。查询文件不足指定条数、出现不符合入口的操作或数据库错误都会报错，不生成 `QUERY_COMPLETE`。脚本不调用 fill、flush 或手动 compaction。原生查询入口要求 `use_existing_db=true`；保留 `readonly=false` 以支持 HotRAP 内部的热度维护和 promotion。

## 与参考查询脚本的参数核对

| 参数 | RocksDB 参考脚本 | PebblesDB 参考脚本 | 本次 HotRAP 查询 |
| --- | --- | --- | --- |
| 查询前清页缓存 | `drop_caches=1` | `drop_caches=1` | 默认 `sync` + `drop_caches=1` |
| num / value size | 1B / 128 | 1B / 128 | 从预加载配置恢复，当前为 1B / 128 |
| 查询数 | 变量 500000，未传入命令 | 显式 500000 | 显式 `ycsb_num=500000` |
| block cache | 8 MiB | 8 MiB | 8 MiB |
| Bloom bits | 10 | 10 | 从预加载配置恢复，当前为 10 |
| open files | 80000 | 1000 | 1000，与 PebblesDB 和本实验 open1000 配置一致 |
| write buffer | 1 MiB | 1 MiB | 1 MiB |
| target SST file size | 1 MiB | 1 MiB | 1 MiB |
| batch size | 1 | 1000，但 YCSB 内层每次只执行 1 个操作 | 1，每个 CSV 操作单独执行 |
| compression | none | 关闭 | 从预加载配置恢复，当前为 none |
| stats interval | 100000000 | 100000000 | 100000000；50 万查询期间不触发周期 stats |
| iostat interval | 100 秒 | 100 秒 | 100 秒，同时采样 nvme0n1 和 pmem0 |
| range length / key offset | 默认只 Seek / +2000000 | 20 / +2000000 | 20 / +2000000，与 PebblesDB 一致 |

HotRAP 的 PM/SSD/RALT 路径、分层容量、hot set/RALT 上限、level base 及后台线程数属于本实验的存储配置，从预加载配置恢复。查询用的 1 MiB 写缓冲和目标 SST 参数单独覆盖；保存的 `preload_config.env` 是预加载配置快照，实际查询参数以日志中的完整命令为准。

## 日志与缓存

默认日志根目录分别为 `HotRAP_zipf_random_read_open1000` 和 `HotRAP_zipf_random_range_read_open1000`。各 workload 的日志包含所用配置、完整命令、操作数、吞吐、延迟分位数和查询结束时的数据库 stats；另存内存日志和 iostat（若已安装）。完整命令保存到日志中，便于检查查询口径。成功后写 `QUERY_COMPLETE`；同一结果存在时跳过，未完成的日志目录不覆盖。

默认 `DROP_CACHES=1`，与参考脚本一致：每个 workload 在启动查询程序、打开数据库之前执行 `sync` 和 `echo 1 > /proc/sys/vm/drop_caches`。需要用 `sudo bash ...` 运行；清缓存权限不足或操作失败会停止，不会继续执行查询。清缓存成功的时间写入该次查询日志。`DRY_RUN=1` 只显示执行计划，不清缓存。显式设置 `DROP_CACHES=0` 可选择不清缓存，日志目录名包含该值，避免与默认冷缓存结果混用。清 Linux 页缓存不重置数据库热度或数据布局；每个查询进程均新建自己的 block cache。

可覆盖的变量：`PRELOAD_ITERATION`、`QUERY_OPS`、`SCAN_LENGTH`、`SCAN_KEY_OFFSET`、`DROP_CACHES`、`DRY_RUN`、`DB_BENCH`、`PRELOAD_ROOT`、`WORKLOAD_ROOT`、`LOG_ROOT`。正常实验应保持默认的预加载目录和 workload 目录；目录覆盖主要用于小规模测试。
