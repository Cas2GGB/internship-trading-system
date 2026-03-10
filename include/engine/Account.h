#pragma once
#include <unordered_map>
#include "Types.h"

/**
 * @brief 账户信息结构体
 * 用于记录用户的可用资金、冻结资金、可用持仓和冻结持仓。
 */
struct Account {
    ClientID id = 0;                                 // 投资者唯一标识符 (客户ID)
    Price balance = 100000000;                       // 账户可用资金余额，默认初始化为 1 亿
    Price frozenFunds = 0;                           // 冻结资金 (当买单挂单未成交时，对应的预估金额被冻结)
    std::unordered_map<StockID, Qty> positions;      // 可用股票持仓 (键为股票ID，值为当前拥有的股票数量)
    std::unordered_map<StockID, Qty> frozenPositions;// 冻结的股票持仓 (当卖单挂单未成交时，对应的股票数量被冻结)
};
