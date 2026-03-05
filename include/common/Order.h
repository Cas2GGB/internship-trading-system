#pragma once
#include "Types.h"

// Forward declaration to avoid circular dependency
struct PriceLevel;

/**
 * @brief Order 结构体
 * 
 * 设计重点：
 * 1. 内存对齐：字段按大小降序排列，减少 padding，尽可能塞进更少的 cache line。
 * 2. 侵入式链表：内嵌 prev/next 指针，支持 O(1) 删除，无需额外的 std::list::node 分配。
 */
struct Order {
    // -------------------------------------------------------------------------
    // 8-byte aligned fields (Core Identity)
    // -------------------------------------------------------------------------
    OrderID id;            // 订单唯一标识
    ClientID clientId;     // 发起该订单的客户ID
    Price price;           // 委托价格
    uint64_t timestamp;    // 委托时间戳 (纳秒)
    
    // -------------------------------------------------------------------------
    // 4-byte aligned fields (Quantity & Instrument)
    // -------------------------------------------------------------------------
    Qty originalQty;       // 原始委托数量
    Qty leavesQty;         // 剩余未成交数量 (初始值等于 originalQty，随成交减少)
    StockID stockId;       // 该订单所属的股票代码

    // -------------------------------------------------------------------------
    // 1-byte aligned fields (Enums)
    // -------------------------------------------------------------------------
    Side side;             // 买卖方向
    OrderType type;        // 订单类型
    TimeInForce timeInForce; // 执行策略 (GTC/IOC/FOK)
    
    // Padding manually if needed, or rely on compiler alignment
    uint8_t _padding[5];   // 使得指针之前的部分对齐，非必须，视编译器而定
    
    // -------------------------------------------------------------------------
    // Pointers (8 Bytes on 64-bit OS) - Intrusive List
    // -------------------------------------------------------------------------
    // 侵入式链表指针：用于在 PriceLevel 中串联同价格订单
    // 这种设计允许我们在 O(1) 时间内从链表中移除特定订单
    Order* prev = nullptr;           // 指向链表中的前一个订单
    Order* next = nullptr;           // 指向链表中的后一个订单

    // 反向指针：指向所属的价格档位，方便撤单时 O(1) 更新档位统计信息
    struct PriceLevel* level = nullptr; 

    // Constructor for convenience (though pool allocators might use placement new)
    Order() = default;
    
    Order(OrderID oid, ClientID cid, StockID sid, Side s, Price p, Qty q, OrderType t)
        : id(oid), clientId(cid), price(p), originalQty(q), leavesQty(q), stockId(sid), 
          side(s), type(t), timeInForce(TimeInForce::GTC) {}
};
