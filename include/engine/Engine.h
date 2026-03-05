#pragma once

#include <string>
#include <unordered_map>
#include "OrderBook.h"

class Engine {
public:
    Engine();
    ~Engine();

    // 初始化引擎 (加载配置等)
    void init(const std::string& configPath);

    // 启动事件循环
    void run();

    // 从脚本文件加载指令
    void loadScript(const std::string& scriptPath);

private:
    // 支持多个标的的 OrderBook 实例 (StockID -> OrderBook*)
    std::unordered_map<StockID, OrderBook*> orderBooks; 
    
    // 辅助函数：按需获取或创建 OrderBook
    OrderBook* getOrderBook(StockID stockId);
    
    // 指令处理
    void processCommand(const std::string& line);
    
    // 快照管理
    void triggerSnapshot();
    
    // 自动快照配置
    uint64_t snapshotInterval = 0; // 0 表示禁用
    uint64_t processedOrderCount = 0; // 当前已处理的 orders 计数

    // 工具函数：字符串分割
    std::vector<std::string> split(const std::string& s, char delimiter);
};
