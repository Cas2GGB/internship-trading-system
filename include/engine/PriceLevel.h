#pragma once
#include <cstddef> // for size_t
#include "Types.h"
#include "Order.h"

// 定义跳表节点的最大层数，避免动态分配指针数组
// 12层可以有效处理 2^12 ~ 4096 个节点，16层 -> 65536 个节点
// 为了安全起见，设置为16
const int SKIPLIST_MAX_LEVEL = 16; 

/**
 * @brief PriceLevel 价格档位
 * 
 * 一个 PriceLevel 代表了 OrderBook 中某一特定价格的所有挂单集合。
 * 它既是一个双向链表的管理者 (Head/Tail)，也是跳表 (SkipList) 的一个节点。
 */
struct PriceLevel {
    // -------------------------------------------------------------------------
    // 订单队列管理 (FIFO)
    // -------------------------------------------------------------------------
    Price price;           // 该档位的价格
    Qty totalQty;          // 该档位所有订单的总挂单量 (用于 L2 行情快照)
    uint32_t orderCount;   // 该档位包含的订单个数 (用于 L2 行情深度分析)
    
    Order* head;           // 链表头指针：指向该价格档位最早加入的订单 (最先成交)
    Order* tail;           // 链表尾指针：指向该价格档位最新加入的订单 (新单插入位置)
    
    // -------------------------------------------------------------------------
    // 跳表节点属性
    // -------------------------------------------------------------------------
    // 跳表的层级索引指针数组，指向下一个 PriceLevel
    // level: 1-based index (尽管数组是 0-based)
    // forward[i] 存储第 i 层的下一个节点指针，每层会有多个PriceLevel
    PriceLevel* forward[SKIPLIST_MAX_LEVEL]; 

    // 构造函数
    PriceLevel(Price p) 
        : price(p), totalQty(0), orderCount(0), head(nullptr), tail(nullptr) {
        for (int i = 0; i < SKIPLIST_MAX_LEVEL; ++i) {
            forward[i] = nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // 侵入式链表操作 (O(1))
    // -------------------------------------------------------------------------
    
    // 将订单加入队列尾部
    void addOrder(Order* order) {
        order->level = this;
        order->next = nullptr;
        order->prev = tail;
        
        if (tail) {
            tail->next = order;
        } else {
            head = order;
        }
        tail = order;
        
        totalQty += order->leavesQty;
        orderCount++;
    }

    // 从队列中移除指定订单 (用于撤单/成交)
    // 如果移除后该价格档位变空，则返回 true
    bool removeOrder(Order* order) {
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            // 是头结点
            head = order->next;
        }
        
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            // 是尾结点
            tail = order->prev;
        }
        
        // 为了安全清空指针
        order->prev = nullptr;
        order->next = nullptr;
        order->level = nullptr;
        
        totalQty -= order->leavesQty;
        orderCount--;
        
        return isEmpty();
    }

    bool isEmpty() const {
        return orderCount == 0;
    }
};
