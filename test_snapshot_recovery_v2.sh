#!/bin/bash
# =============================================================================
# test_snapshot_recovery_v2.sh
# 快照恢复功能验证脚本（大数据量版）
#
# 实验设计：
#   Baseline : ENABLE_SNAPSHOT=0，一口气跑完全部 100 万订单，末尾输出所有账户状态
#   Fork     : ENABLE_SNAPSHOT=1，在第 50 万条订单处自动触发快照，然后中断进程；
#              新起一个进程 RESTORE，再把后半段数据灌入，末尾同样输出所有账户状态
#   比对     : diff 两组实验中所有账户的 Balance / FrozenFunds / 持仓，完全一致 => 快照功能正确
# =============================================================================
set -e

PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$PROJECT_ROOT"

# ----------------------------------------------------------------------------
# 0. 编译
# ----------------------------------------------------------------------------
echo "========================================"
echo "0. Building C++ Engine"
echo "========================================"
cd build && make -j$(nproc) 2>&1 | tail -5 && cd ..

# ----------------------------------------------------------------------------
# 1. 生成测试数据（幂等：已存在则跳过重新生成，加 --force 参数可强制重新生成）
# ----------------------------------------------------------------------------
DATA_FILE="test/snapshot_test_data.txt"
SNAP_DIR="test/snapshot_recovery_result"
mkdir -p "$SNAP_DIR"

if [[ ! -f "$DATA_FILE" ]] || [[ "$1" == "--regen" ]]; then
    echo "========================================"
    echo "1. Generating test data (1M orders)"
    echo "========================================"
    python3 test/gen_snapshot_test.py
else
    echo "1. Using existing data file: $DATA_FILE"
    echo "   (Pass --regen to regenerate)"
fi

TOTAL_LINES=$(wc -l < "$DATA_FILE")
echo "   Data file: $DATA_FILE ($TOTAL_LINES lines)"

# 把数据文件拆成两半：前半段（用于快照前喂入）+ 后半段（恢复后继续喂入）
# 策略：在文件中找到第 500000 条 ADD 命令所在的行号，从那里切分
# （GIVE_POS 行不算在 ADD 计数里）
echo "   Splitting data into part1 / part2 at order 500000..."
PART1_FILE="$SNAP_DIR/part1.txt"
PART2_FILE="$SNAP_DIR/part2.txt"

python3 - <<'SPLIT_EOF'
import sys, os

data_file  = "test/snapshot_test_data.txt"
part1_file = "test/snapshot_recovery_result/part1.txt"
part2_file = "test/snapshot_recovery_result/part2.txt"

# 先统计文件中实际有多少条 ADD，再取中间点切割
# 这样无论数据量多少都能正确对半切
all_lines = open(data_file).readlines()
add_lines = [l for l in all_lines if l.startswith("ADD ")]
total_adds = len(add_lines)
SPLIT_AT = max(1, total_adds // 2)  # 取一半
print(f"   Total ADD orders in file: {total_adds}, splitting at: {SPLIT_AT}")

add_count  = 0
part1_lines = []
part2_lines = []
split_done  = False

for line in all_lines:
    stripped = line.strip()
    # ACCOUNT 命令始终放 part2（末尾汇总，不参与切割计数）
    if stripped.startswith("ACCOUNT"):
        part2_lines.append(line)
        continue
    if split_done:
        part2_lines.append(line)
    else:
        part1_lines.append(line)
        if stripped.startswith("ADD "):
            add_count += 1
            if add_count >= SPLIT_AT:
                # 在 part1 末尾插入 SNAPSHOT，后续订单进 part2
                part1_lines.append("SNAPSHOT\n")
                split_done = True

with open(part1_file, 'w') as f:
    f.writelines(part1_lines)
with open(part2_file, 'w') as f:
    f.writelines(part2_lines)

print(f"   Part1: {len(part1_lines)} lines (incl. SNAPSHOT at ADD#{SPLIT_AT})")
print(f"   Part2: {len(part2_lines)} lines (incl. {sum(1 for l in part2_lines if l.startswith('ACCOUNT'))} ACCOUNT queries)")
SPLIT_EOF

# ----------------------------------------------------------------------------
# 2. Baseline（关闭快照，一次性跑完）
# ----------------------------------------------------------------------------
echo ""
echo "========================================"
echo "2. Running Baseline (no snapshot)"
echo "========================================"

# 关闭快照，开启压测模式
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=0/' conf/matching_engine.conf
sed -i 's/^ENABLE_STRESS_TEST=.*/ENABLE_STRESS_TEST=1/' conf/matching_engine.conf

rm -f "$SNAP_DIR/baseline_raw.log"

{ cat "$DATA_FILE"; echo "EXIT"; } \
    | bin/matching_engine \
    | grep -E "^\[Engine\] Account " \
    > "$SNAP_DIR/baseline_raw.log"

echo "   Baseline done. Account lines: $(wc -l < "$SNAP_DIR/baseline_raw.log")"

# 对账户输出排序（保证比对时顺序一致）
sort "$SNAP_DIR/baseline_raw.log" > "$SNAP_DIR/baseline.log"
echo "   Sorted baseline saved to: $SNAP_DIR/baseline.log"

# ----------------------------------------------------------------------------
# 3. Fork 实验（开启快照，前半段喂入后中断，新进程 RESTORE 再继续）
# ----------------------------------------------------------------------------
echo ""
echo "========================================"
echo "3. Running Fork Snapshot + Restore"
echo "========================================"

# 开启快照
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=1/' conf/matching_engine.conf

# 清理旧快照
rm -f test/snapshot_*.dat
rm -f "$SNAP_DIR/fork_raw.log"

# 3.1 阶段一：喂入 part1（含 SNAPSHOT 指令），喂完即 EXIT
#     SNAPSHOT 是异步 fork 子进程写盘，EXIT 之前给子进程一点时间完成写盘
echo "   [3.1] Phase 1: feeding part1 + triggering snapshot..."
{ cat "$PART1_FILE"; echo "SLEEP 2000"; echo "EXIT"; } \
    | bin/matching_engine \
    | grep -v "^\[Perf\]" \
    > "$SNAP_DIR/fork_phase1.log" 2>&1

# 确认快照文件已写出
SNAP_COUNT=$(ls test/snapshot_*.dat 2>/dev/null | wc -l)
if [[ "$SNAP_COUNT" -eq 0 ]]; then
    echo "   [ERROR] No snapshot files found after phase 1! Aborting."
    exit 1
fi
echo "   [3.1] Done. Snapshot files: $SNAP_COUNT"
ls -lh test/snapshot_*.dat

# 3.2 阶段二：新起进程，先 RESTORE，再喂入 part2（后半段订单 + ACCOUNT 命令）
echo "   [3.2] Phase 2: new process RESTORE + feeding part2..."
{ echo "RESTORE"; cat "$PART2_FILE"; echo "EXIT"; } \
    | bin/matching_engine \
    | grep -E "^\[Engine\] Account " \
    > "$SNAP_DIR/fork_raw.log" 2>&1

echo "   Fork done. Account lines: $(wc -l < "$SNAP_DIR/fork_raw.log")"

# 对 fork 输出排序
sort "$SNAP_DIR/fork_raw.log" > "$SNAP_DIR/fork.log"
echo "   Sorted fork output saved to: $SNAP_DIR/fork.log"

# ----------------------------------------------------------------------------
# 4. 比对
# ----------------------------------------------------------------------------
echo ""
echo "========================================"
echo "4. Comparing Results"
echo "========================================"

BASELINE_LINES=$(wc -l < "$SNAP_DIR/baseline.log")
FORK_LINES=$(wc -l < "$SNAP_DIR/fork.log")
echo "   Baseline account lines : $BASELINE_LINES"
echo "   Fork account lines     : $FORK_LINES"

if cmp -s "$SNAP_DIR/baseline.log" "$SNAP_DIR/fork.log"; then
    echo ""
    echo "  [SUCCESS] 快照恢复验证通过！"
    echo "  Baseline 与 Fork 实验的所有账户资金和持仓状态完全一致。"
else
    echo ""
    echo "  [FAILED] 账户状态不一致！差异如下（< baseline  > fork）："
    diff "$SNAP_DIR/baseline.log" "$SNAP_DIR/fork.log" | head -80 || true
    echo ""
    echo "  完整 diff 已保存："
    diff "$SNAP_DIR/baseline.log" "$SNAP_DIR/fork.log" > "$SNAP_DIR/diff.log" 2>&1 || true
    echo "  $SNAP_DIR/diff.log"
fi

# 恢复配置为默认（关闭快照）
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=0/' conf/matching_engine.conf

echo ""
echo "All done. Results in: $SNAP_DIR/"
