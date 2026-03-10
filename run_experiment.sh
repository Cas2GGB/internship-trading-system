#!/bin/bash
# Stop on error
set -e

# Define paths (Relative to project root)
CONF_FILE="conf/matching_engine.conf"
DATA_FILE="test/full_test.txt"
PLOT_SCRIPT="test/plot_results_v2.py"

# Check data
if [ ! -f "$DATA_FILE" ]; then
    echo "Error: Test data $DATA_FILE not found!"
    echo "Trying to generate data..."
    python3 test/gen_data.py
    if [ ! -f "$DATA_FILE" ]; then
        echo "Generation failed."
        exit 1
    fi
fi

# Define output directory
LOG_DIR="test"
mkdir -p "$LOG_DIR"

# 清理旧日志（如果存在）
rm -f "$LOG_DIR/baseline.log" "$LOG_DIR/experiment.log" "$LOG_DIR/control.log"
# 清理快照文件以节省空间
rm -f snapshot_*.dat core.*

echo "=================================================="
echo "1. Baseline Phase"
echo "   Condition: No Snapshots"
echo "=================================================="
sed -i 's/^SNAPSHOT_INTERVAL=.*/SNAPSHOT_INTERVAL=0/' "$CONF_FILE"
# Disable snapshot completely for baseline execution
if grep -q "ENABLE_STRESS_TEST" "$CONF_FILE"; then
    sed -i "s/^ENABLE_STRESS_TEST=.*/ENABLE_STRESS_TEST=1/" "$CONF_FILE"
else
    echo "ENABLE_STRESS_TEST=1" >> "$CONF_FILE"
fi

if grep -q "ENABLE_SNAPSHOT" "$CONF_FILE"; then
    sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=0/' "$CONF_FILE"
else
    echo "ENABLE_SNAPSHOT=0" >> "$CONF_FILE"
fi

# Run in background to be consistent
bin/matching_engine < "$DATA_FILE" > "$LOG_DIR/baseline.log" &
PID=$!
wait $PID

echo "=================================================="
echo "2. Experiment Phase"
echo "   Condition: Internal Fork Snapshot (Once, via manual command)"
echo "=================================================="
# Disable automatic snapshot (interval=0), rely on the injected SNAPSHOT command
sed -i 's/^SNAPSHOT_INTERVAL=.*/SNAPSHOT_INTERVAL=0/' "$CONF_FILE"
# Enable snapshot capability for this group
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=1/' "$CONF_FILE"

# Run in background to be consistent
bin/matching_engine < "$DATA_FILE" > "$LOG_DIR/experiment.log" &
PID=$!
wait $PID

# 立即清理快照以释放空间
rm -f snapshot_*.dat

echo "=================================================="
echo "3. Control Group Phase"
echo "   Condition: External Gcore Snapshot (Once)"
echo "=================================================="
# 禁用内部快照
sed -i 's/^SNAPSHOT_INTERVAL=.*/SNAPSHOT_INTERVAL=0/' "$CONF_FILE"
# Disable internal snapshot capability so it IGNORES the manual SNAPSHOT command in the file
sed -i 's/^ENABLE_SNAPSHOT=.*/ENABLE_SNAPSHOT=0/' "$CONF_FILE"

# 在后台运行引擎
# Use direct input redirection, Engine will ignore SNAPSHOT command because ENABLE_SNAPSHOT=0
bin/matching_engine < "$DATA_FILE" > "$LOG_DIR/control.log" &
ENGINE_PID=$!

echo "   Engine running with PID $ENGINE_PID. Waiting for orders to reach 7,000,000..."

# 高效等待循环：每 0.1秒检查一次日志
MAX_WAIT=60  # Increased wait time for bloat phase
WAITED=0

# Step 1: Wait for Metrics Reset (Phase 1 completion)
echo "   Waiting for Phase 1 (Bloat) to complete..."
while ! grep -q "Metrics reset" "$LOG_DIR/control.log"; do
    sleep 0.1
    WAITED=$((WAITED+1))
    if ! kill -0 $ENGINE_PID 2>/dev/null; then
        echo "   Error: Engine process died during Phase 1!"
        break 2
    fi
    if [ "$WAITED" -ge "$((MAX_WAIT*10))" ]; then
        echo "   Timeout waiting for Phase 1."
        break 2
    fi
done

echo "   Phase 1 complete. Waiting for Phase 2 orders..."
WAITED=0

# Step 2: Wait for 3M orders in Phase 2 (Early trigger to catch the process)
# We wait until "Orders: 3000000" appears AT LEAST TWICE (once in Phase 1, once in Phase 2)
# Phase 2 has 15M orders, so triggering at 3M leaves 12M orders (~10 seconds) for gcore to run.
TARGET_ORDERS="Orders: 3000000"
while [ "$(grep -c "$TARGET_ORDERS" "$LOG_DIR/control.log")" -lt 2 ]; do
    sleep 0.1
    WAITED=$((WAITED+1))
    if ! kill -0 $ENGINE_PID 2>/dev/null; then
        echo "   Error: Engine process died during Phase 2!"
        break
    fi
    if [ "$WAITED" -ge "$((MAX_WAIT*10))" ]; then
        echo "   Timeout waiting for Phase 2 orders."
        break
    fi
done

# 触发外部快照
echo "   !!! Triggering GCORE (Real) !!!"

# 使用 gcore 对进程进行快照 (这会暂停进程直到内存转储完成)
# 注意：生成的 core 文件可能会很大，我们随后立即删除
gcore $ENGINE_PID > /dev/null 2>&1 || true

# 立即清理 core dump 文件以节省磁盘空间
rm -f core.$ENGINE_PID core

# 立即清理 core dump 文件以节省磁盘空间
rm -f core.$ENGINE_PID core

# 等待引擎结束
wait $ENGINE_PID
echo "   Control group finished."

echo "=================================================="
echo "4. Generating Report"
echo "=================================================="
python3 "$PLOT_SCRIPT"
echo "Done. Check test/tps_comparison.png, test/latency_max.png, test/latency_avg.png"
