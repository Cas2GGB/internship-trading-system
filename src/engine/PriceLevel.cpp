#include "PriceLevel.h"

PriceLevel::PriceLevel(Price p) 
    : price(p), totalQty(0), orderCount(0), head(nullptr), tail(nullptr) {
    for (int i = 0; i < SKIPLIST_MAX_LEVEL; ++i) {
        forward[i] = nullptr;
    }
}

void PriceLevel::addOrder(Order* order) {
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

bool PriceLevel::removeOrder(Order* order) {
    if (order->prev) {
        order->prev->next = order->next;
    } else {
        // 是该档位的第一个元素
        head = order->next;
    }
    
    if (order->next) {
        order->next->prev = order->prev;
    } else {
        // 是该档位的最后一个元素
        tail = order->prev;
    }
    
    // 安全清空指针，切断与其相关联的上下文环境
    order->prev = nullptr;
    order->next = nullptr;
    order->level = nullptr;
    
    totalQty -= order->leavesQty;
    orderCount--;
    
    return isEmpty();
}

bool PriceLevel::isEmpty() const {
    return orderCount == 0;
}
