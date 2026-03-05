#pragma once
#include <cstdint>
#include <limits>

// -----------------------------------------------------------------------------
// Basic Type Definitions
// -----------------------------------------------------------------------------

using OrderID = uint64_t;      // 订单ID，全局唯一，递增
using ClientID = uint64_t;     // 客户ID，用于区分不同用户的委托
using StockID = uint32_t;      // 股票代码，用于索引不同的订单簿
using Price = int64_t;         // 价格，放大10000倍的定点数 (20.5 -> 205000)
using Qty = uint32_t;          // 数量，通常为正整数

// 无效/空值定义
constexpr OrderID INVALID_ORDER_ID = 0;
constexpr Price INVALID_PRICE = std::numeric_limits<Price>::min();
constexpr Qty INVALID_QTY = 0;

// -----------------------------------------------------------------------------
// Enumerations (Using uint8_t to save memory)
// -----------------------------------------------------------------------------

enum class Side : uint8_t { 
    UNKNOWN = 0,
    BUY = 1,  // 买单
    SELL = 2  // 卖单
};

enum class OrderType : uint8_t { 
    UNKNOWN = 0,
    LIMIT = 1,  // 限价单：指定价格成交
    MARKET = 2, // 市价单：按当前最优价成交
    CANCEL = 3  // 撤单指令
};

enum class TimeInForce : uint8_t { 
    GTC = 0, // Good-Till-Cancel: 一直有效直到由于交易或取消
    IOC = 1, // Immediate-Or-Cancel: 立即成交否则取消剩余部分
    FOK = 2  // Fill-Or-Kill: 全部成交否则全部取消
};

// -----------------------------------------------------------------------------
// Helper to string
// -----------------------------------------------------------------------------

inline const char* sideToString(Side s) {
    if (s == Side::BUY) return "BUY";
    if (s == Side::SELL) return "SELL";
    return "UNKNOWN";
}
