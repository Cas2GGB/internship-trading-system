import random
import os

# =============================================================================
# 配置区：模拟多账户、多股票的真实压测场景
# =============================================================================

# 股票列表：股票ID -> 基准价格
# 模拟 A 股不同板块的股票，价格分布差异明显
STOCKS = {
    1:  100,    # 大盘蓝筹，价格约100
    2:  50,     # 中盘成长，价格约50
    3:  200,    # 高价白马，价格约200
    4:  20,     # 低价题材，价格约20
    5:  500,    # 高价龙头，价格约500
    6:  80,     # 科技成长，价格约80
    7:  30,     # 小盘股，价格约30
    8:  150,    # 消费白马，价格约150
    9:  10,     # 低价ST股，价格约10
    10: 300,    # 医药龙头，价格约300
}

# 账户数量
NUM_ACCOUNTS = 200

# 每个账户初始资金范围（模拟散户、机构等不同量级）
ACCOUNT_BALANCE_TIERS = [
    (0.40, 500_000,    2_000_000),    # 40% 散户：50万~200万
    (0.35, 2_000_000,  20_000_000),   # 35% 中型投资者：200万~2000万
    (0.15, 20_000_000, 100_000_000),  # 15% 机构：2000万~1亿
    (0.10, 100_000_000,500_000_000),  # 10% 大机构：1亿~5亿
]

# 每个账户初始持有各股票的数量范围（手，1手=100股）
INIT_POSITION_LOTS_RANGE = (0, 5000)  # 0~5000手，即0~50万股


def pick_balance():
    """按比例随机选取一个初始资金"""
    r = random.random()
    cumulative = 0.0
    for prob, lo, hi in ACCOUNT_BALANCE_TIERS:
        cumulative += prob
        if r < cumulative:
            return random.randint(lo, hi)
    return random.randint(500_000, 2_000_000)


def generate_full_test(filename, bloat_count=8_000_000, trade_count=15_000_000, match_rate=0.8):
    """
    生成多账户、多股票的真实压测数据。

    参数：
        bloat_count  : 第一阶段内存填充订单数量，控制快照文件大小。
        trade_count  : 第二阶段压测订单数量，控制压测时长。
        match_rate   : 第二阶段中主动成交订单的比例 (0.0~1.0)。
                       成交单会穿越对手盘价格确保立即成交；
                       挂单不会与对手盘重叠，确保一定挂簿。
    """
    print(f"正在生成多账户多股票真实压测数据，目标文件：{filename}")
    print(f"  股票数量：{len(STOCKS)} 支，账户数量：{NUM_ACCOUNTS} 个")
    print(f"  撮合比例：{match_rate:.0%}，填充订单数：{bloat_count:}，压测订单数：{trade_count:}")

    stock_ids = list(STOCKS.keys())
    account_ids = list(range(1, NUM_ACCOUNTS + 1))

    # ------------------------------------------------------------------
    # 为每支股票预计算价格区间
    # 挂单区间与对手方分离，避免无意撮合
    # 成交单打穿对手方价格区间，保证立即撮合
    # ------------------------------------------------------------------
    stock_price_config = {}
    for sid, base in STOCKS.items():
        spread = max(int(base * 0.05), 2)   # 5% 价差，至少2
        gap    = max(int(base * 0.12), 5)   # 买卖之间留12%的价差空隙
        passive_buy_lo  = base - gap - spread
        passive_buy_hi  = base - gap
        passive_sell_lo = base + gap
        passive_sell_hi = base + gap + spread
        aggr_buy_lo     = passive_sell_lo
        aggr_buy_hi     = passive_sell_hi
        aggr_sell_lo    = passive_buy_lo
        aggr_sell_hi    = passive_buy_hi
        stock_price_config[sid] = (
            passive_buy_lo, passive_buy_hi,
            passive_sell_lo, passive_sell_hi,
            aggr_buy_lo, aggr_buy_hi,
            aggr_sell_lo, aggr_sell_hi,
        )

    # 统计变量（用于末尾汇总）
    give_pos_count = 0
    account_balances = {aid: pick_balance() for aid in account_ids}

    with open(filename, 'w') as f:

        # ==================================================================
        # 初始化阶段：给每个账户预分配持仓（GIVE_POS），模拟开盘前持仓状态
        # 只有有持仓才能在第二阶段挂卖单，否则引擎会拒绝
        # ==================================================================
        print(f"[初始化] 正在为 {NUM_ACCOUNTS} 个账户分配 {len(stock_ids)} 支股票的初始持仓...")
        for aid in account_ids:
            for sid in stock_ids:
                # 随机决定该账户是否持有该股票（60% 概率持有）
                if random.random() < 0.60:
                    lots = random.randint(INIT_POSITION_LOTS_RANGE[0] + 1, INIT_POSITION_LOTS_RANGE[1])
                    qty = lots * 100  # 转换为股数
                    f.write(f"GIVE_POS {aid} {sid} {qty}\n")
                    give_pos_count += 1
        print(f"[初始化] 完成，共生成 {give_pos_count:} 条持仓记录")

        # ==================================================================
        # 第一阶段：虚盘填充，深度价外订单，目的是撑大内存占用（快照文件大小）
        # 买单挂在价格极低处，卖单挂在价格极高处，永远不会成交
        # 分散到多支股票、多个账户，贴近真实场景
        # ==================================================================
        print(f"[第一阶段] 正在生成 {bloat_count:} 条深度价外虚盘订单（跨 {len(stock_ids)} 支股票）...")
        for i in range(bloat_count):
            oid  = i + 1
            sid  = stock_ids[i % len(stock_ids)]   # 轮询分配股票
            aid  = account_ids[i % NUM_ACCOUNTS]   # 轮询分配账户
            base = STOCKS[sid]
            if i % 2 == 0:
                side  = 1  # 买单
                price = random.randint(1, max(1, int(base * 0.01)))     # 极低价，永不成交
            else:
                side  = 2  # 卖单
                price = random.randint(int(base * 20), int(base * 21))  # 极高价，永不成交
            qty = 100
            f.write(f"ADD {sid} {oid} {price} {qty} {side} {aid}\n")
        print(f"[第一阶段] 完成，共 {bloat_count:} 条虚盘订单")

        # RESET 性能指标，第二阶段开始前清零
        f.write("RESET_METRICS\n")

        # ==================================================================
        # 第二阶段：真实压测订单，模拟多账户活跃交易
        # - 多账户随机下单，模拟真实资金流
        # - 多股票随机选择，模拟组合交易
        # - match_rate 控制撮合压力
        # - 加入撤单、即撤单等操作模拟真实行为
        # ==================================================================
        print(f"[第二阶段] 正在生成 {trade_count:} 条压测订单"
              f"（撮合比例 {match_rate:.0%}，{len(stock_ids)} 支股票 × {NUM_ACCOUNTS} 个账户）...")
        start_id = bloat_count + 1

        # 用于即撤单的临时订单 ID 偏移基数，避免与正常订单冲突
        fake_oid_base    = start_id + trade_count + 10_000_000
        fake_oid_counter = 0
        cancel_fail_count   = 0   # 追撤（大概率失败）次数
        cancel_success_count = 0  # 即撤（大概率成功）次数

        for i in range(trade_count):
            if i == 2_000_000:
                f.write("SNAPSHOT\n")
                print(f"[第二阶段] 已处理 200万条，触发快照指令")

            oid  = start_id + i
            sid  = random.choice(stock_ids)     # 随机股票
            aid  = random.choice(account_ids)   # 随机账户
            side = random.choice([1, 2])        # 买/卖
            qty  = random.randint(1, 10) * 100  # 1~10手，100的倍数

            cfg = stock_price_config[sid]
            passive_buy_lo,  passive_buy_hi  = cfg[0], cfg[1]
            passive_sell_lo, passive_sell_hi = cfg[2], cfg[3]
            aggr_buy_lo,     aggr_buy_hi     = cfg[4], cfg[5]
            aggr_sell_lo,    aggr_sell_hi    = cfg[6], cfg[7]

            if random.random() < match_rate:
                # 主动成交单（Aggressive）：价格穿越对手盘，确保立即撮合
                if side == 1:
                    price = random.randint(aggr_buy_lo,  aggr_buy_hi)
                else:
                    price = random.randint(aggr_sell_lo, aggr_sell_hi)
            else:
                # 被动挂单（Passive）：价格远离对手盘，挂入订单簿
                if side == 1:
                    price = random.randint(passive_buy_lo,  passive_buy_hi)
                else:
                    price = random.randint(passive_sell_lo, passive_sell_hi)

            f.write(f"ADD {sid} {oid} {price} {qty} {side} {aid}\n")

            # ------------------------------------------------------------------
            # 随机撤单行为，模拟真实市场
            # ------------------------------------------------------------------
            rand_val = random.random()
            if rand_val < 0.05:
                # 追撤：尝试撤销一个大概率已成交的近期订单（撤单大概率失败）
                cancel_oid = oid - random.randint(100, 500)
                if cancel_oid > start_id:
                    f.write(f"CANCEL {sid} {cancel_oid}\n")
                    cancel_fail_count += 1
            elif rand_val < 0.10:
                # 即撤：下一笔极深价外单后立即撤销（模拟算法交易的探测行为，撤单必然成功）
                fake_oid   = fake_oid_base + fake_oid_counter
                fake_oid_counter += 1
                f.write(f"ADD {sid} {fake_oid} 1 100 1 {aid}\n")  # 极低挂买，永不成交
                f.write(f"CANCEL {sid} {fake_oid}\n")
                cancel_success_count += 1

        print(f"[第二阶段] 完成，共 {trade_count:} 条压测订单")
        print(f"  其中追撤单（大概率失败）：{cancel_fail_count:} 条")
        print(f"  其中即撤单（必然成功）  ：{cancel_success_count:} 条")

    # ==================================================================
    # 汇总报告
    # ==================================================================
    print()
    print("="*60)
    print("              生成数据概况汇总")
    print("="*60)
    print(f"  输出文件        : {filename}")
    print(f"  股票数量        : {len(STOCKS)} 支")
    for sid, base in STOCKS.items():
        cfg = stock_price_config[sid]
        print(f"    股票{sid:>2d}  基准价={base:>4d}  "
              f"挂买区间=[{cfg[0]},{cfg[1]}]  挂卖区间=[{cfg[2]},{cfg[3]}]")
    print(f"  账户数量        : {NUM_ACCOUNTS} 个（ID 1~{NUM_ACCOUNTS}）")
    print(f"  账户资金分布    : 散户(40%) 50万~200万 | 中型(35%) 200万~2000万"
          f" | 机构(15%) 2000万~1亿 | 大机构(10%) 1亿~5亿")
    print(f"  初始持仓记录    : {give_pos_count:} 条（每账户60%概率持有各股票，0~50万股）")
    print(f"  第一阶段虚盘    : {bloat_count:} 条（深度价外，用于撑大内存/快照体积）")
    print(f"  第二阶段压测    : {trade_count:} 条")
    print(f"    撮合比例      : {match_rate:.0%}（主动成交单）/ {1-match_rate:.0%}（被动挂单）")
    print(f"    每笔数量      : 1~10手（100~1000股）")
    print(f"    追撤单        : 约5%概率，大概率失败（目标订单已成交）")
    print(f"    即撤单        : 约5%概率，必然成功（模拟算法探测行为）")
    print("="*60)


if __name__ == "__main__":
    dir_path = os.path.dirname(os.path.abspath(__file__))
    output_path = os.path.join(dir_path, "full_test.txt")
    generate_full_test(output_path)
  