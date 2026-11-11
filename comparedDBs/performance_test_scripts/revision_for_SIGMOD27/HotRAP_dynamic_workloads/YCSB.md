# HotRAP YCSB A–F

六个独立脚本 `zipf_workload_YCSB_A.sh` 至 `zipf_workload_YCSB_F.sh` 使用 HotRAP 原生 `tools/db_bench_tool.cc` 中的 CSV 执行入口。默认使用 Zipf 1.2 的 1B 预加载副本 `iter03` 作为源，为每个 workload 分别复制独立工作库，再运行对应 CSV。源库保留，用于后续重复实验。

## 运行

在本目录执行：

```bash
bash build_hotrap_ycsb.sh

# 只显示源库、复制目标、查询参数；不复制、不清缓存、不打开数据库。
DRY_RUN=1 bash zipf_workload_YCSB_A.sh

sudo bash zipf_workload_YCSB_A.sh
sudo bash zipf_workload_YCSB_B.sh
sudo bash zipf_workload_YCSB_C.sh
sudo bash zipf_workload_YCSB_D.sh
sudo bash zipf_workload_YCSB_E.sh
sudo bash zipf_workload_YCSB_F.sh

# 指定其他 Zipf；仍为 A/B/.../F 分别复制独立工作库：
sudo bash zipf_workload_YCSB_A.sh 1.2 1.3 1.4

# 重复一次实验：复制新工作库并保留上次结果。
sudo env RUN_ID=run02 bash zipf_workload_YCSB_F.sh 1.2
```

每组都从相同源库开始，运行顺序不会改变其他组的初始数据库。源库须完成预加载，并且没有正在运行的数据库进程；默认选择留作备用的 `iter03`。可用 `PRELOAD_ITERATION` 指定另一份源库。为了比较起点一致的实验，应选择未执行过业务负载的预加载副本。

`build_hotrap_ycsb.sh` 生成 `share/hotrap/build-experiments/db_bench_ycsb_csv`。原来的预加载程序 `db_bench` 和点查/范围查询程序 `db_bench_read_csv` 不会被此构建命令替换；执行逻辑仍全部位于同一份 `db_bench_tool.cc` 源码中。

## CSV 与执行语义

当前 `/mnt/workloads` 中的实际文件已核对：

| Workload | benchmark | CSV | 原始记录数 | 默认有效执行数 | 清洗后操作组成 |
| --- | --- | --- | --- | --- | --- |
| A | `ycsba` | `ycsb_a_workload.csv` | 1000000 | 999999 | READ 499999、UPDATE 500000 |
| B | `ycsbb` | `ycsb_b_workload.csv` | 1000000 | 999999 | READ 949999、UPDATE 50000 |
| C | `ycsbc` | `ycsb_c_workload.csv` | 950000 | 949999 | READ 949999 |
| D | `ycsbd` | `ycsb_d_workload.csv` | 1000000 | 1000000 | READ 950000、INSERT 50000 |
| E | `ycsbe` | `ycsb_e_workload.csv` | 1000000 | 999999 | SCAN 949999、INSERT 50000 |
| F | `ycsbf` | `ycsb_f_workload.csv` | 1000000 | 1000000 | READ 500000、RMW 500000 |

真实输入中 A 第 292703 行、B 第 607741 行、C 第 90649 行含 `READ,Key`；E 第 965315 行含 `SCAN,Key`。这些是数据区混入的表头字符串，不能作为数字 key 执行。2026-11-06 的 A/B/C `run01` 因此在执行部分操作后失败。

`prepare_ycsb_csv.py` 在复制数据库之前完整校验输入；默认 `YCSB_OPS=auto`，使用有效记录数。仅去除数据区明确的 `Key` 表头残留（包括重复的 `Operation,Key`），保留原 CSV；其他非法 key、操作、字段数或 uint64 越界均直接报错。显式指定 `YCSB_OPS` 时必须不超过有效记录数，不会静默减少请求量。

每次实验将规范化的副本保存为日志目录下的 `prepared_ycsb_<A-F>.csv`，`workload_validation.json` 记录源文件、原始行数、有效行数、剔除行号与原因、有效操作组成和实际执行数。db_bench 只读取此副本。工作库名增加 `csvchecked`，不会复用或覆盖之前已部分执行的失败库；修复后可直接重新运行原命令，从预加载源重新复制。

CSV 有 `Operation,Key` 表头，每条记录按文件顺序执行。key 使用与预加载一致的十进制 ASCII 编码，左侧补零到 16 字节，不按 `num` 取模。

- READ：执行 Get，记录命中数；未命中也计一次操作。
- UPDATE / INSERT：执行 Put，生成 128 字节 value（实际大小从预加载配置恢复）。
- E 的 SCAN：起始 key 为 CSV key + 2000000，每次新建 iterator，Seek 后读取最多 20 条，包括 Seek 定位到的第一条，到末尾可以不足 20 条，然后释放 iterator。与已确认的 PebblesDB 范围查询语义一致。**偏移只用于 SCAN，INSERT 使用 CSV 原始 key。**
- F 的 RMW：先 Get，命中后生成新 value 并 Put 回同一个 key；未命中时不新增 key，与已有 RocksDB/PebblesDB 的实现一致。整条 RMW（Get + 可选 Put）只计一次操作，延迟覆盖完整过程。汇总分别输出 `rmw_ops`、`rmw_found`、`rmw_writes`。

A/B 只接受 READ/UPDATE，C 只接受 READ，D 只接受 READ/INSERT，E 只接受 SCAN/INSERT，F 只接受 READ/RMW。除已记录并剔除的表头残留外，行格式错误、无效操作或 key 会在复制前拒绝；文件短于显式指定条数或数据库错误也会退出，不生成完成标记。原生入口要求 `--use_existing_db=true --readonly=false --threads=1 --batch_size=1`，避免错误初始化或重复回放。F 使用单线程，与参考执行流程一致。

## 参数与独立工作库

通用执行参数沿用刚确认的查询实验设置：8 MiB block cache、`open_files=1000`、1 MiB write buffer、1 MiB target SST、无压缩、Bloom bits 10、单线程、单操作 batch、`histogram=1`、`stats_interval=100000000`。后台线程从源配置恢复，当前为 6 compaction + 1 flush，`max_background_jobs=7`。调用 `ycsbX,stats`，不追加 fill、手动 flush 或手动 compaction。

PM/SSD 容量、hot set/RALT 上限、level base 及其他 HotRAP 配置从源 `preload_config.env` 恢复，不因 YCSB 的操作数重新估算容量。当前源配置：

| Zipf | PM 快层 | NVM 慢层 | level base |
| --- | --- | --- | --- |
| 1.2 | 640 MiB | 6400 MiB | 32 MiB |
| 1.3 | 160 MiB | 1600 MiB | 8 MiB |
| 1.4 | 64 MiB | 640 MiB | 4 MiB |

工作库的四个目录如下；每个 case 包含 workload、Zipf、源 iteration、操作数、查询参数和 RUN_ID：

```text
/mnt/nvm/hotrap1B_ycsb/db/<case>/
/mnt/pmem0/hotrap1B_ycsb/tier0/<case>/
/mnt/nvm/hotrap1B_ycsb/tier1/<case>/
/mnt/pmem0/hotrap1B_ycsb/ralt/<case>/
```

复制时使用与 RocksDB 一致的 fcntl 数据库锁，发现源库正在使用则停止。四个目录分别执行 `cp -a --reflink=auto`，不使用硬链接；支持 reflink 的文件系统采用写时复制，否则实际复制文件。目标 `db_paths` 保持 tier0 / tier1 的顺序，容量与源库相同。

复制完成后、打开工作库之前，默认执行 **`sync` → `echo 1 > /proc/sys/vm/drop_caches` → 启动 db_bench**，并在日志记录清缓存成功时间。复制不计入 benchmark 的查询吞吐。清缓存失败立即停止。`DRY_RUN=1` 不清缓存；`DROP_CACHES=0` 仅供明确选择不清缓存或临时小规模测试使用。

## 输出和重跑

日志目录为 `1B_HotRAP_YCSB_<A-F>_PM_NVM_PaperRatio_open1000/<case>/`，包含完整命令、原始预加载配置、工作库的 `run_config.env`、输入校验报告和准备后的 CSV、操作数与吞吐、延迟直方图、数据库 stats，以及 iostat / perf / 内存日志（工具存在时）。iostat 每 100 秒采样 nvme0n1 和 pmem0；perf 默认开启，可设 `PERF=0` 关闭。perf 权限或采样失败记录到单独文件，不替代 benchmark 的退出码。

`COPY_COMPLETE` 表示工作库已复制，`QUERY_STARTED` 表示业务操作已开始，`QUERY_COMPLETE` 仅在退出码与操作数校验均通过后生成。已完成结果自动跳过；已有未完成目录不覆盖。失败工作库和日志保留；新一轮实验设置新的 `RUN_ID`，再次从源库复制。脚本不会自动删除任何数据库。

可覆盖变量：`PRELOAD_ITERATION`、`RUN_ID`、`YCSB_OPS`、`YCSB_DATA_FILE`、`SCAN_LENGTH` / `SCAN_KEY_OFFSET`（仅 E）、`DROP_CACHES`、`DRY_RUN`、`PERF`、`DB_BENCH`、`PRELOAD_ROOT`、`YCSB_NVM_ROOT`、`YCSB_PM_ROOT`、`LOG_ROOT`。

源码修改保存在完整补丁 `hotrap_csv_bench.patch` 和对上一版查询代码的增量补丁 `hotrap_csv_ycsb.patch` 中。三个构建脚本均能识别并应用尚未安装的 CSV/read/YCSB 补丁。
