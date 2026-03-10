#!/bin/bash
set -e

PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $PROJECT_ROOT

echo "========================================"
echo "1. Rebuilding C++ Engine with Global RESTORE command"
echo "========================================"
cd build && make -j4 && cd ..

# 设置一个完全干净的日志存放目录（现在只会产生极少的最终比对日志）
TEST_DIR="test/snapshot_test"
mkdir -p $TEST_DIR
rm -f $TEST_DIR/*

# =========================================================================
# 2. 内联定义测试数据 (流式传输，不再生成大量碎片文件)
# =========================================================================

# 上半场：建仓、发生部分成交流转、制造游离冻结资金和剩余订单
read -r -d '' PART1 << 'DATA_EOF' || true
GIVE_POS 10 1 100
GIVE_POS 11 1 100
GIVE_POS 12 1 100
GIVE_POS 13 1 100
ACCOUNT 10
ACCOUNT 11
ADD 1 100 100 50 1 10
ADD 1 101 99 50 1 11
ADD 1 102 101 50 2 12
ADD 1 103 102 50 2 13
ADD 1 200 101 20 1 10
CANCEL 1 102
DATA_EOF

# 下半场：中间态验证后，继续激烈交易，穿透盘口，然后做最终清结算
read -r -d '' PART2 << 'DATA_EOF' || true
ADD 1 300 105 100 2 13
ACCOUNT 10
ACCOUNT 12
ADD 1 400 90 50 2 13
CANCEL 1 100
CANCEL 1 300
ACCOUNT 10
ACCOUNT 11
ACCOUNT 12
ACCOUNT 13
DATA_EOF

# =========================================================================
# 3. 运行 Baseline (无快照，一口气跑完)
# =========================================================================
echo "========================================"
echo "2. Running Baseline (Continuous execution)"
echo "========================================"
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=0/' conf/matching_engine.conf

# 直接利用 bash echo 和管道把上下半场的数据一次性灌入系统
echo -e "$PART1\n$PART2\nEXIT" | bin/matching_engine | grep -E "(\[Engine\] 发生成交|撤单|RESTORE|System Restore|Global Restore|ACCOUNT|Account)" > $TEST_DIR/baseline.log

echo "   -> Baseline run finished. (Logs saved to $TEST_DIR/baseline.log)"

# =========================================================================
# 4. 运行 Fork 内存快照与恢复断点续传
# =========================================================================
rm -f test/snapshot_*.dat
echo "========================================"
echo "3. Running Internal Fork Snapshot & Restore"
echo "========================================"
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=1/' conf/matching_engine.conf

# 阶段 3.1: 启动进程 1 号，只喂入 Part 1，紧接着打快照并退出
echo -e "$PART1\nSNAPSHOT\nSLEEP 500\nEXIT" | bin/matching_engine | grep -E "(\[Engine\] 发生成交|撤单|RESTORE|System Restore|Global Restore|ACCOUNT|Account)" > $TEST_DIR/fork_run.log
echo "   -> Snapshot generated locally."

# 阶段 3.2: 启动进程 2 号（全新），发送一键恢复，然后紧挨着喂入 Part 2
echo -e "RESTORE\n$PART2\nEXIT" | bin/matching_engine | grep -E "(\[Engine\] 发生成交|撤单|RESTORE|System Restore|Global Restore|ACCOUNT|Account)" >> $TEST_DIR/fork_run.log
echo "   -> Restore & Post-run finished. (Logs appended to $TEST_DIR/fork_run.log)"

# =========================================================================
# 5. 比对结果
# =========================================================================
echo "========================================"
echo "4. Comparing Results (Diff)"
echo "========================================"

# 去除恢复提示特有的字眼，保证上下两面对称
grep -v "Global Restore" $TEST_DIR/fork_run.log | grep -v "System Global Restore" > $TEST_DIR/fork_run_clean.log

if cmp -s $TEST_DIR/baseline.log $TEST_DIR/fork_run_clean.log; then
    echo "  [SUCCESS] 极致严谨：内存快照断点恢复的交易最终状态，与从头跑到底的基准状态精确一致！"
else
    echo "  [FAILED] Logs mismatch! Checking lines:"
    diff -y $TEST_DIR/baseline.log $TEST_DIR/fork_run_clean.log || true
fi
