#include "OrderBook.h"
#include "AccountManager.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

OrderBook::OrderBook(StockID id, AccountManager* accMgr) : stockId(id), accountManager(accMgr) {
    // 初始时最优买卖盘应为空
}

OrderBook::~OrderBook() {
    // 清理所有订单
    for (auto& pair : orderMap) {
        orderPool.release(pair.second); // 释放回对象池
    }
    orderMap.clear();
}

// -----------------------------------------------------------------------------
// Core Trading Logic
// -----------------------------------------------------------------------------

void OrderBook::addOrder(const Order& incomingOrder) {
    // 1. 从对象池获取一个临时订单对象
    Order* order = orderPool.acquire(incomingOrder);
    
    // 2. 尝试立即撮合 (Eating the liquidity)
    matchOrder(order);
    
    // 3. 如果完全成交，不需要加入订单簿
    if (order->leavesQty == 0) {
        orderPool.release(order);
        return;
    }
    
    // 4. 如果未完全成交，将剩余部分加入订单簿
    orderMap[order->id] = order;
    
    PriceLevel* level = nullptr;
    if (order->side == Side::BUY) {
        // 插入买单簿
        level = bids.insert(order->price);
    } else {
        // 插入卖单簿
        level = asks.insert(order->price);
    }
    
    if (level) {
        level->addOrder(order);
    }
    
    // 5. 更新缓存
    updateBestCache();
}

void OrderBook::matchOrder(Order* order) {
    // 当订单还有未成交量时
    while (order->leavesQty > 0) {
        // 找到最优对手盘价格
        PriceLevel* bestOpposingLevel = nullptr;
        
        if (order->side == Side::BUY) {
            // 买方寻找最低卖价 (asks.begin() 为最小值)
            bestOpposingLevel = asks.begin();
            
            // 如果没有卖单，或者最优卖价高于我的买入限价，则无法成交
            // 只有当 best ask <= my bid price 时成交
            if (!bestOpposingLevel || bestOpposingLevel->price > order->price) {
                break; 
            }
        } else {
            // 卖方寻找最高买价 (bids.begin() 为最大值)
            bestOpposingLevel = bids.begin();
            
            // 如果没有买单，或者最优买价低于我的卖出限价，则无法成交
            // 只有当 best bid >= my sell price 时成交
            if (!bestOpposingLevel || bestOpposingLevel->price < order->price) {
                break;
            }
        }
        
        // 遍历该价格档位的订单 (FIFO)
        // 简单实现：使用指针遍历，真实系统可能更复杂
        Order* bookOrder = bestOpposingLevel->head;
        while (bookOrder && order->leavesQty > 0) {
            
            // 计算最大可成交量
            Qty matchQty = std::min(order->leavesQty, bookOrder->leavesQty);
            
            // 执行成交
            order->leavesQty -= matchQty;
            bookOrder->leavesQty -= matchQty;
            
            // 更新统计数据
            lastTradePrice = bestOpposingLevel->price;
            lastTradeQty = matchQty;
            
            // 记录日志：输出详细交易信息
            std::cout << "[Engine] 发生成交: Stock " << stockId 
                      << " | 主动单(Taker) " << order->id 
                      << " vs 被动单(Maker) " << bookOrder->id 
                      << " | 成交数量: " << matchQty 
                      << " | 成交价格: " << lastTradePrice << std::endl;
            
            if (accountManager) {
                auto processTrade = [&](Order* o, Qty qty, Price px) {
                    Account& acc = accountManager->getAccount(o->clientId);
                    if (o->side == Side::BUY) {
                        acc.frozenFunds -= (qty * o->price);
                        acc.positions[stockId] += qty;
                        // 退回差价 (限价 - 实际成交价)
                        acc.balance += (qty * (o->price - px));
                    } else { // SELL
                        acc.frozenPositions[stockId] -= qty;
                        acc.balance += (qty * px);
                    }
                };
                // 处理主动单和被动单的资金
                processTrade(order, matchQty, lastTradePrice);
                processTrade(bookOrder, matchQty, lastTradePrice);
            }

            // 如果订单簿中的订单完全成交，移除它
            if (bookOrder->leavesQty == 0) {
                Order* filledOrder = bookOrder;
                bookOrder = bookOrder->next; // 提前移动指针
                
                // 从订单簿清理已成交订单
                // 1. 从链表移除
                bestOpposingLevel->removeOrder(filledOrder);
                // 2. 从哈希表移除
                orderMap.erase(filledOrder->id);
                // 3. 释放对象内存
                orderPool.release(filledOrder); // 归还对象池
                
                // 如果该价格档位为空，从跳表移除
                if (bestOpposingLevel->isEmpty()) {
                    if (order->side == Side::BUY) {
                        asks.removeIfEmpty(bestOpposingLevel);
                    } else {
                        bids.removeIfEmpty(bestOpposingLevel);
                    }
                    // 当前 level 消失，跳出内循环刷新 bestOpposingLevel
                    break; 
                }
            } else {
                // 如果对手盘订单只是部分成交，说明进来的订单已经完全成交了
                // 此时 order->leavesQty 为 0，可以直接退出循环
                break;
            }
        }
    }
}

bool OrderBook::cancelOrder(OrderID orderId) {
    auto it = orderMap.find(orderId);
    if (it == orderMap.end()) {
        return false;
    }
    
    Order* order = it->second;
    PriceLevel* level = order->level;
    
    if (level) {
        bool levelEmpty = level->removeOrder(order);
        if (levelEmpty) {
            if (order->side == Side::BUY) {
                bids.removeIfEmpty(level);
            } else {
                asks.removeIfEmpty(level);
            }
        }
    }
    
    orderMap.erase(it);
    // delete order; 
    // Replaced with ObjectPool
    orderPool.release(order);
    
    updateBestCache();
    return true;
}

void OrderBook::updateBestCache() {
    bestBid = !bids.empty() ? bids.begin() : nullptr;
    bestAsk = !asks.empty() ? asks.begin() : nullptr;
}

// -----------------------------------------------------------------------------
// Snapshot / Persistence
// -----------------------------------------------------------------------------

// 常量定义，方便版本管理
const uint32_t SNAPSHOT_MAGIC = 0x534E4150; // "SNAP"
const uint32_t SNAPSHOT_VERSION = 1;

struct SnapshotHeader {
    uint32_t magic;      // 魔数，标识文件格式
    uint32_t version;    // 版本号，用于兼容性检查
    StockID stockId;     // 股票/合约ID
    uint64_t orderCount; // 订单总数，用于读取时的循环计数
};

// 简单的平面 POD 结构，用于磁盘序列化
struct OrderEntry {
    OrderID id;              // 订单ID
    ClientID clientId;       // 客户ID
    Price price;             // 委托价格
    uint64_t timestamp;      // 委托时间戳
    Qty originalQty;         // 原始委托量
    Qty leavesQty;           // 剩余未成交量
    StockID stockId;         // 股票ID
    Side side;               // 买卖方向
    OrderType type;          // 订单类型
    TimeInForce timeInForce; // 执行策略
};

void OrderBook::saveSnapshot(const std::string& filename) {
    std::ofstream ofs(filename, std::ios::binary | std::ios::out);
    if (!ofs.is_open()) return;
    
    SnapshotHeader header;
    header.magic = SNAPSHOT_MAGIC; 
    header.version = SNAPSHOT_VERSION;
    header.stockId = stockId;
    header.orderCount = orderMap.size();
    
    ofs.write(reinterpret_cast<char*>(&header), sizeof(header));
    
    // 【优化】分块缓冲写入 (Chunked Buffer)，防止由于全量 reserve 在极限场景下导致 OOM
    const size_t CHUNK_SIZE = 100000; // 单次落盘批次大小 (约 4~5MB)
    std::vector<OrderEntry> buffer;
    buffer.reserve(CHUNK_SIZE);
    // 我们必需按照 价格/时间 优先级遍历，以确保恢复的确定性
    // 1. 买单 (High to Low, then FIFO)
    PriceLevel* curr = bids.begin();
    while (curr) {
        Order* o = curr->head;
        while (o) {
            OrderEntry entry;
            entry.id = o->id;
            entry.clientId = o->clientId;
            entry.price = o->price;
            entry.timestamp = o->timestamp;
            entry.originalQty = o->originalQty;
            entry.leavesQty = o->leavesQty;
            entry.stockId = o->stockId;
            entry.side = o->side;
            entry.type = o->type;
            entry.timeInForce = o->timeInForce;
            
            buffer.push_back(entry);
            
            // 如果缓冲区满了，立即刷盘并清空游标，从而持续复用这几 M 的内存
            if (buffer.size() >= CHUNK_SIZE) {
                ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(OrderEntry));
                buffer.clear();
            }
            
            o = o->next;
        }
        // 移动到下一个价格档位
        // SkipList::forward[0] 是第0层的下一个节点 (即连接所有节点的链表)
        curr = curr->forward[0]; 
    }
    
    // 2. 卖单 (Low to High, then FIFO)
    curr = asks.begin();
    while (curr) {
        Order* o = curr->head;
        while (o) {
            OrderEntry entry;
            entry.id = o->id;
            entry.clientId = o->clientId;
            entry.price = o->price;
            entry.timestamp = o->timestamp;
            entry.originalQty = o->originalQty;
            entry.leavesQty = o->leavesQty;
            entry.stockId = o->stockId;
            entry.side = o->side;
            entry.type = o->type;
            entry.timeInForce = o->timeInForce;
            
            buffer.push_back(entry);
            
            // 如果缓冲区满了，立即刷盘并清空游标，从而持续复用这几 M 的内存
            if (buffer.size() >= CHUNK_SIZE) {
                ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(OrderEntry));
                buffer.clear();
            }
            
            o = o->next;
        }
        curr = curr->forward[0];
    }
    
    if (!buffer.empty()) {
        ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(OrderEntry));
    }
    
    ofs.close();
}

void OrderBook::loadSnapshot(const std::string& filename) {
    std::ifstream ifs(filename, std::ios::binary | std::ios::in);
    if (!ifs.is_open()) return;
    
    SnapshotHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.magic != SNAPSHOT_MAGIC) return;
    if (header.version != SNAPSHOT_VERSION) {
        // TODO: 处理版本升级兼容性
        return; 
    }
    
    // 如果有的话，清除当前状态（简单实现假设为空簿）
    
    // 【加载优化】使用大块缓冲区流式读取订单数据，大幅削减 read 系统调用
    const size_t CHUNK_SIZE = 100000;
    std::vector<OrderEntry> buffer(CHUNK_SIZE);
    
    size_t ordersRead = 0;
    while (ordersRead < header.orderCount) {
        size_t toRead = std::min(CHUNK_SIZE, static_cast<size_t>(header.orderCount - ordersRead));
        ifs.read(reinterpret_cast<char*>(buffer.data()), toRead * sizeof(OrderEntry));
        
        for (size_t i = 0; i < toRead; ++i) {
            const OrderEntry& entry = buffer[i];
        
        // 重建订单 - 使用对象池
        Order* order = orderPool.acquire(); 
        
        order->id = entry.id;
        order->clientId = entry.clientId;
        order->price = entry.price;
        order->timestamp = entry.timestamp;
        order->originalQty = entry.originalQty;
        order->leavesQty = entry.leavesQty;
        order->stockId = entry.stockId;
        order->side = entry.side;
        order->type = entry.type;
        order->timeInForce = entry.timeInForce;
        
        // 直接插入而不撮合（恢复状态）
        orderMap[order->id] = order;
        
        PriceLevel* level = nullptr;
        if (order->side == Side::BUY) {
            level = bids.insert(order->price);
        } else {
            level = asks.insert(order->price);
        }
        
        if (level) {
            level->addOrder(order);
        }
        } // inner chunk loop
        ordersRead += toRead;
    } // outer chunk loop
    
    updateBestCache();
    ifs.close();
}
