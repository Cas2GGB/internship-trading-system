#!/bin/bash
set -e
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $PROJECT_ROOT

echo "========================================"
echo "1. 重新编译核心引擎"
echo "========================================"
cd build && make -j4 && cd ..

echo "========================================"
echo "2. 运行撮合与撤单功能测试"
echo "========================================"
sed -i "s/^ENABLE_STRESS_TEST=.*/ENABLE_STRESS_TEST=0/" conf/matching_engine.conf
LOG_FILE="test/functional_result.log"
EXPECTED_FILE="test/expected_functional_result.log"

bin/matching_engine < test/test_trading.txt > $LOG_FILE
cat $LOG_FILE

echo ""
echo "========================================"
echo "3. 验证正确性 (比对基准结果)"
echo "========================================"
set +e
diff -u $EXPECTED_FILE $LOG_FILE
if [ $? -eq 0 ]; then
    echo "✅ 测试通过！账户资金结算与撤单解冻逻辑完全正确，计算过程均符合预期。"
else
    echo "❌ 测试失败！当前输出的结果与预期的准确账本不同，请检查代码变更是否破坏了结算逻辑。"
    exit 1
fi
set -e

echo "========================================"
echo "流程测试完成。"
echo "========================================"
