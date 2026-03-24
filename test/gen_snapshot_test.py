#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_snapshot_test.py
生成用于快照恢复验证的真实交易数据（100万订单）。

设计原则：
  - 多账户（200个）× 多股票（10支），贴近真实场景
  - 初始资金按散户/机构分层，初始持仓随机但合理
  - 订单价格在合法区间内，资金/持仓校验不会大量被拒
  - 主动撮合订单约占 70%，保证成交率
  - 末尾输出所有账户状态，供 baseline vs fork 比对
  - 生成的数据文件直接通过管道灌入 bin/matching_engine
"""

import random
import os
import sys

# =============================================================================
# 股票配置：10 支，基准价格覆盖低/中/高价段
# =============================================================================
STOCKS = {
    1:  100,
    2:  50,
    3:  200,
    4:  20,
    5:  500,
    6:  80,
    7:  30,
    8:  150,
    9:  10,
    10: 300,
}

# =============================================================================
# 账户配置
# =============================================================================
NUM_ACCOUNTS = 200

# 初始资金分层（比例, 下限, 上限），单位：元（整数）
BALANCE_TIERS = [
    (0.40,  500_000,    2_000_000),   # 40% 散户
    (0.35,  2_000_000,  20_000_000),  # 35% 中型
    (0.15,  20_000_000, 100_000_000), # 15% 机构
    (0.10,  100_000_000,500_000_000), # 10% 大机构
]

# 初始持仓：每账户对每支股票必然持仓，持仓量 50000~500000 股（足够支撑全程卖单）
HOLD_PROB      = 1.00
MIN_INIT_QTY   = 50_000
MAX_INIT_QTY   = 500_000

# 订单总量
TOTAL_ORDERS   = 1_000_000

# 主动成交比例（穿越对手盘，保证立即撮合）
MATCH_RATE     = 0.70

# 撤单概率（追撤已成交单，大概率失败；即撤新挂单，必然成功）
CHASE_CANCEL_PROB  = 0.04
INSTANT_CANCEL_PROB = 0.04


def pick_balance(rng: random.Random) -> int:
    r = rng.random()
    cumulative = 0.0
    for prob, lo, hi in BALANCE_TIERS:
        cumulative += prob
        if r < cumulative:
            return rng.randint(lo, hi)
    return rng.randint(500_000, 2_000_000)


def build_price_config(stocks: dict) -> dict:
    """
    为每支股票生成挂单区间和主动成交区间。
    买卖之间留 12% 空隙防止被动挂单意外撮合。
    """
    cfg = {}
    for sid, base in stocks.items():
        spread = max(int(base * 0.05), 2)
        gap    = max(int(base * 0.12), 5)
        # 被动挂单区间
        pb_lo = max(1, base - gap - spread)
        pb_hi = max(1, base - gap)
        ps_lo = base + gap
        ps_hi = base + gap + spread
        # 主动成交区间（穿越对手盘）
        ab_lo = ps_lo
        ab_hi = ps_hi
        as_lo = pb_lo
        as_hi = pb_hi
        cfg[sid] = (pb_lo, pb_hi, ps_lo, ps_hi, ab_lo, ab_hi, as_lo, as_hi)
    return cfg


def generate(output_path: str, seed: int = 42):
    rng = random.Random(seed)

    stock_ids   = list(STOCKS.keys())
    account_ids = list(range(1, NUM_ACCOUNTS + 1))
    price_cfg   = build_price_config(STOCKS)

    # 本地追踪持仓（近似值，卖单用；买单不预校验，交由引擎处理）
    positions:  dict[int, dict[int, int]] = {}  # aid -> sid -> qty

    print(f"[gen] 目标文件：{output_path}")
    print(f"[gen] 股票数：{len(STOCKS)}，账户数：{NUM_ACCOUNTS}，订单数：{TOTAL_ORDERS:,}")

    with open(output_path, 'w') as f:

        # ----------------------------------------------------------------
        # 初始化：GIVE_POS（持仓）
        # AccountManager 默认给每个账户 1 亿初始资金，这里不额外 GIVE 资金
        # 但我们本地要跟踪资金，默认用 1 亿，然后按各账户分层做本地记账
        # ----------------------------------------------------------------
        print("[gen] 生成初始持仓...")
        give_pos_count = 0
        for aid in account_ids:
            positions[aid] = {}
            for sid in stock_ids:
                # 每个账户对每支股票都给足初始持仓，避免卖单被引擎拒绝
                qty = rng.randint(MIN_INIT_QTY, MAX_INIT_QTY)
                # 对齐到 100 的整数倍（1手=100股）
                qty = (qty // 100) * 100
                positions[aid][sid] = qty
                f.write(f"GIVE_POS {aid} {sid} {qty}\n")
                give_pos_count += 1
        print(f"[gen] 持仓记录：{give_pos_count:,} 条")

        # ----------------------------------------------------------------
        # 生成 100 万订单
        # ----------------------------------------------------------------
        print(f"[gen] 生成 {TOTAL_ORDERS:,} 条订单...")

        # 用于即撤的虚单 ID 起点（避免与正常订单 ID 冲突）
        fake_oid_base    = TOTAL_ORDERS + 10_000_000
        fake_oid_counter = 0
        written = 0

        for i in range(TOTAL_ORDERS):
            oid  = i + 1
            sid  = rng.choice(stock_ids)
            aid  = rng.choice(account_ids)
            side = rng.choice([1, 2])
            qty  = rng.randint(1, 10) * 100  # 1~10 手

            cfg = price_cfg[sid]
            pb_lo, pb_hi, ps_lo, ps_hi, ab_lo, ab_hi, as_lo, as_hi = cfg

            if rng.random() < MATCH_RATE:
                # 主动成交单：价格穿越对手盘，保证立即撮合
                price = rng.randint(ab_lo, ab_hi) if side == 1 else rng.randint(as_lo, as_hi)
            else:
                # 被动挂单：价格远离对手盘，挂入订单簿
                price = rng.randint(pb_lo, pb_hi) if side == 1 else rng.randint(ps_lo, ps_hi)

            # 卖单：本地近似追踪持仓，持仓不足时跳过（初始持仓很大，跳过率极低）
            if side == 2:
                avail = positions[aid].get(sid, 0)
                if avail < qty:
                    continue
                positions[aid][sid] = avail - qty
            # 买单：不做本地资金校验，引擎有 1 亿初始资金，正常价格买单不会耗尽

            f.write(f"ADD {sid} {oid} {price} {qty} {side} {aid}\n")
            written += 1

            # 随机撤单
            rv = rng.random()
            if rv < CHASE_CANCEL_PROB:
                # 追撤：目标是若干步前的订单（大概率已成交，撤单失败是正常的）
                target = oid - rng.randint(50, 300)
                if target >= 1:
                    f.write(f"CANCEL {sid} {target}\n")
            elif rv < CHASE_CANCEL_PROB + INSTANT_CANCEL_PROB:
                # 即撤：下一笔极低价挂买单后立即撤，必然成功
                fake_oid = fake_oid_base + fake_oid_counter
                fake_oid_counter += 1
                f.write(f"ADD {sid} {fake_oid} 1 100 1 {aid}\n")
                f.write(f"CANCEL {sid} {fake_oid}\n")

        print(f"[gen] 订单生成完毕，实际写入：{written:,} 条")

        # ----------------------------------------------------------------
        # 末尾：输出所有账户状态，供比对用
        # ----------------------------------------------------------------
        print("[gen] 生成 ACCOUNT 查询命令（所有账户）...")
        for aid in account_ids:
            f.write(f"ACCOUNT {aid}\n")

    print(f"[gen] 完成，文件路径：{output_path}")
    print(f"[gen] 账户数：{NUM_ACCOUNTS}，股票数：{len(STOCKS)}")
    print(f"[gen] 可用于：cat {output_path} | bin/matching_engine")


if __name__ == "__main__":
    dir_path  = os.path.dirname(os.path.abspath(__file__))
    out_file  = os.path.join(dir_path, "snapshot_test_data.txt")
    seed_val  = int(sys.argv[1]) if len(sys.argv) > 1 else 42
    generate(out_file, seed=seed_val)
