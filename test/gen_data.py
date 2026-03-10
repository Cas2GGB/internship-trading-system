import random
import os

def generate_full_test(filename, bloat_count=8000000, trade_count=15000000):
    print(f"Generating data to {filename}...")
    
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
        print(f"Phase 2: Generating {trade_count} trade orders (Matching) for throughput test...")
        # 成交订单：接近最优买卖价，价格区间95-105。高成交概率。
        start_id = bloat_count + 1
        for i in range(trade_count):
            if i == 2000000:
                f.write("SNAPSHOT\n")
            oid = start_id + i
            side = random.choice([1, 2])
            price = random.randint(95, 105) 
            qty = random.randint(1, 10)
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
                f.write(f"ADD 1 {fake_oid} 1 100 1\n") # 买一价格，远离95-105区间
                f.write(f"CANCEL 1 {fake_oid}\n")
            
    print(f"Done. Total orders: {bloat_count + trade_count}")

if __name__ == "__main__":
    # Ensure output directory exists
    dir_path = os.path.dirname(__file__)
    if not dir_path: dir_path = "."
    output_path = os.path.join(dir_path, "full_test.txt")
    generate_full_test(output_path)
