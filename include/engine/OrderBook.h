#pragma once

#include <unordered_map>
#include <string>
#include "Types.h"
#include "Order.h"
#include "SkipList.h"
#include "ObjectPool.h"

// -----------------------------------------------------------------------------
// 订单簿类定义
// -----------------------------------------------------------------------------

class OrderBook {
private:
    StockID stockId; 
    
    // 核心存储
    SkipList<Greater> bids;  // 买单：价格优先（高->低）
    SkipList<Less> asks;     // 卖单：价格优先（低->高）
    
    // 订单对象池
    ObjectPool<Order> orderPool;
    
    // 缓存最优买卖盘
    PriceLevel* bestBid = nullptr;
    PriceLevel* bestAsk = nullptr;
    
    // 索引：通过 OrderID 快速查找
    std::unordered_map<OrderID, Order*> orderMap;

    // 统计数据
    Price lastTradePrice = 0; 
    Qty lastTradeQty = 0;     

public:
    OrderBook(StockID id);
    ~OrderBook();

    // 核心交互接口
    void addOrder(const Order& order);
    bool cancelOrder(OrderID orderId);
    
    // 快照管理
    void saveSnapshot(const std::string& filename);
    void loadSnapshot(const std::string& filename);
    
    // Getters 用于检查
    Price getBestBid() const { return bestBid ? bestBid->price : 0; }
    Price getBestAsk() const { return bestAsk ? bestAsk->price : 0; }
    size_t getOrderCount() const { return orderMap.size(); }

private:
    // 内部逻辑
    void matchOrder(Order* order);
    void updateBestCache();
};
