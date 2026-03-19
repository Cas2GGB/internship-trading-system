import random
import os

def generate_full_test(filename, bloat_count=8000000, trade_count=15000000, match_rate=0.3):
    """
    生成测试数据。

    参数：
        bloat_count  : Phase 1 内存填充订单数量，控制快照文件大小。
        trade_count  : Phase 2 压测订单数量，控制压测时长。
        match_rate   : Phase 2 中主动成交订单的比例 (0.0~1.0)。
                       成交单会穿越对手盘价格确保立即成交；
                       挂单不会与对手盘重叠，确保一定挂簿。
                       调整此参数可独立控制撮合压力，不影响内存占用。
    """
    print(f"Generating data to {filename}...")
    print(f"  match_rate={match_rate:.0%}, bloat={bloat_count}, trade={trade_count}")

    # Phase 2 价格区间设计（买卖分离，永不自然对撞）：
    #   挂单买区间: 90~94    挂单卖区间: 106~110
    #   成交买区间: 106~110  (高于卖挂单，确保穿价成交)
    #   成交卖区间: 90~94    (低于买挂单，确保穿价成交)
    PASSIVE_BUY_LO,  PASSIVE_BUY_HI  = 90,  94
    PASSIVE_SELL_LO, PASSIVE_SELL_HI = 106, 110
    # 成交单打到对手盘所在区间的另一侧，保证一定能撮合
    AGGR_BUY_LO,  AGGR_BUY_HI  = PASSIVE_SELL_LO, PASSIVE_SELL_HI
    AGGR_SELL_LO, AGGR_SELL_HI = PASSIVE_BUY_LO,  PASSIVE_BUY_HI

    with open(filename, 'w') as f:
        print(f"Phase 1: Generating {bloat_count} bloat orders (Deep OTM) to bloat memory...")
        # 虚盘订单：深度价外单，用于填充内存消耗
        # 在1-10买入，在2000-2100卖出。永远不会成交。
        for i in range(bloat_count):
            oid = i + 1
            if i % 2 == 0:
                side = 1 # Buy
                price = random.randint(1, 10)
            else:
                side = 2 # Sell
                price = random.randint(2000, 2100)
            qty = 100
            f.write(f"ADD 1 {oid} {price} {qty} {side}\n")
        # RESET Metrics
        f.write("RESET_METRICS\n")
        print(f"Phase 2: Generating {trade_count} trade orders (match_rate={match_rate:.0%}) for throughput test...")
        start_id = bloat_count + 1
        for i in range(trade_count):
            if i == 2000000:
                f.write("SNAPSHOT\n")
            oid = start_id + i
            side = random.choice([1, 2])  # 1=Buy, 2=Sell
            qty = random.randint(1, 10)

            # 根据 match_rate 决定这笔单是主动成交单还是被动挂单
            if random.random() < match_rate:
                # 主动成交单（Aggressive）：价格穿越对手盘，确保立即撮合
                if side == 1:  # 买单打高价，吃掉挂卖区间的单
                    price = random.randint(AGGR_BUY_LO, AGGR_BUY_HI)
                else:          # 卖单打低价，吃掉挂买区间的单
                    price = random.randint(AGGR_SELL_LO, AGGR_SELL_HI)
            else:
                # 被动挂单（Passive）：价格远离对手盘，不会成交，挂进订单簿
                if side == 1:  # 买单挂低价
                    price = random.randint(PASSIVE_BUY_LO, PASSIVE_BUY_HI)
                else:          # 卖单挂高价
                    price = random.randint(PASSIVE_SELL_LO, PASSIVE_SELL_HI)

            f.write(f"ADD 1 {oid} {price} {qty} {side}\n")

            # 加入部分撤单数据
            rand_val = random.random()
            if rand_val < 0.05:
                # 随机尝试撤销一个近期下达的且大概率已撮合成交的单子（撤单失败，因为已经成交或不存在）
                cancel_id = oid - random.randint(100, 500)
                if cancel_id > start_id:
                    f.write(f"CANCEL 1 {cancel_id}\n")
            elif rand_val < 0.10:
                # 下达一个永远不成交的深度价外单，随后立即撤销它（撤单成功，因为未成交就被撤了）
                fake_oid = oid + 100000000
                f.write(f"ADD 1 {fake_oid} 1 100 1\n")  # 买一价格，远离挂单区间
                f.write(f"CANCEL 1 {fake_oid}\n")

    print(f"Done. Total orders: {bloat_count + trade_count}")

if __name__ == "__main__":
    # Ensure output directory exists
    dir_path = os.path.dirname(__file__)
    if not dir_path: dir_path = "."
    output_path = os.path.join(dir_path, "full_test.txt")
    generate_full_test(output_path)
