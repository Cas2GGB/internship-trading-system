# 高频交易系统原型：快速内存快照与撮合引擎

本项目是一个基于 C++17 实现的高性能撮合引擎原型，核心目标是**探索并实现一种微秒级的“无卡顿”内存快照方案**。在传统的高频交易系统中，无论是保存业务状态以便于故障恢复，还是给风控系统提供实时快照，都容易因为数据序列化和落盘的 I/O 阻塞导致服务卡顿。本项目通过 `fork()` 操作系统的写时复制（Copy-On-Write, COW）机制，成功将百兆级别快照的毛刺时间从**秒级降至极低（实测 < 10 毫秒）**。

## 🌟 项目亮点

1. **极致性能设计**
   - **对象内存池 (`ObjectPool`)**：避免高频执行 `new/delete`，杜绝内存碎片。
   - **侵入式链表 (`Order`)**：订单对象天然同时也是双向链表的节点，最大程度利用 CPU Cache Line。
   - **自定义跳表 (`SkipList`)**：替代 `std::map` 实现 O(logN) 的价格查询与遍历，同时支持自定义升降序规则。
   - **哈希索引**：支持基于 `OrderID` 对订单进行 O(1) 复杂度的瞬时撤单（Cancel）。

2. **新型内存快照方案 (Cow-based Snapshot)**
   - **Baseline（无快照）**: 提供纯净吞吐量对比。
   - **Control（传统方案/外部快照）**: 模拟诸如 `gcore` 这种挂起整个应用进程的快照方式，暴露其会导致极大延迟（测试中达 1.16 秒）的痛点。
   - **Experiment（本机基于 fork 快照）**: 利用 `fork()` 系统调用，父进程在仅仅消耗纳秒~微秒级 fork 时间后便继续撮合逻辑，所有重量级的序列化（Serialize）和磁盘 I/O 全部由挂靠了写时复制虚拟内存映射的子进程完成。

## 📁 目录结构

```text
.
├── bin/                 # 编译得到的可执行文件
├── build/               # CMake 编译缓存目录
├── conf/                # 配置文件目录 (matching_engine.conf)
├── doc/                 # 项目文档与实验分析
├── include/             # C++ 头文件 (包含 common 和 engine)
├── src/                 # C++ 源代码
├── test/                # 用于生成测试数据、Python绘图和存储实验结果的目录
├── CMakeLists.txt       # CMake 物理配置文件
└── run_experiment.sh    # 一键式自动化综合测试脚本
```

## 🚀 快速开始

### 1. 环境依赖
* Linux 系统 (推荐 Ubuntu)
* `g++` (支持 C++17) 或 `clang++`
* `cmake` (3.10+)
* Python 3 & `matplotlib` 库 (仅运行一键基准测试图使用)
* 开启 `ptrace` 权限 (用于对照组模拟 `gcore` 的执行，非必须，可由 `run_experiment.sh` 在代码层主动开启)：`echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope`。

### 2. 编译项目

在项目根目录执行：
```bash
mkdir -p build && cd build
cmake ..
make -j4
cd ..
```
编译成功后，可执行文件会被输出到 `bin/matching_engine`。

### 3. 一键效果演示与实验数据收集

为了印证快照方案对性能的提升，可运行根目录提供的评估脚本：

```bash
chmod +x run_experiment.sh
./run_experiment.sh
```

此脚本会自动执行以下流程：
1. 若无测试数据，会调用 `test/gen_data.py` 生成一份长达数千万条指令的测试流 `full_test.txt`。
2. 依次运行 **Baseline (无快照)**、**Experiment (内部 Fork 快照)** 和 **Control (外部强停快照)** 三个对照阶段，模拟压力测试过程中的撮合延迟。
3. 从日志中提取吞吐(TPS)与延迟指标。
4. 调用 Python 脚本将实验结果图表（如 `tps_comparison.png`, `latency_max.png`）输出并保存至 `test/` 目录下。

### 4. 手动运行使用

直接运行引擎，可以通过标准输入 (stdin) 手动键入命令进行调试：
```bash
./bin/matching_engine
```

**支持的命令：**
* `ADD <StockID> <OrderID> <Price> <Qty> <Side(1:买/2:卖)> <Type>` - 下单
  * *例如: `ADD 1 101 2500 100 1 1` (用户1发出一笔代码1的买单，价2500，量100)*
* `CANCEL <StockID> <OrderID>` - 撤单
  * *例如: `CANCEL 1 101`*
* `SNAPSHOT` - 手动请求执行写时复制快照。生成的文件将保存在当前目录下。

## 📄 文档索引

关于代码细节设计（系统执行流程、订单簿结构、并发策略等）以及更多实验课题理论，详情请访问 [doc 目录](doc/) 下的详细说明文档。
