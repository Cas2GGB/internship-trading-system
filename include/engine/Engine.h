#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include "OrderBook.h"
#include "AccountManager.h"

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
    AccountManager accountManager;

    // 支持多个标的的 OrderBook 实例 (StockID -> OrderBook*)
    std::unordered_map<StockID, OrderBook*> orderBooks; 
    
    // 支持的股票列表 (白名单)
    std::unordered_set<StockID> supportedStocks;
    
    // 辅助函数：按需获取或创建 OrderBook
    OrderBook* getOrderBook(StockID stockId);
    
    // 指令处理
    void processCommand(const std::string& line);
    
    // 快照管理
    void triggerSnapshot();
    
    // 自动快照配置
    uint64_t snapshotInterval = 0; // 0 表示禁用
    bool enableSnapshot = true;    // 新增：全局是否允许快照
    uint64_t processedOrderCount = 0; // 当前已处理的 orders 计数

    // 工具函数：字符串分割
    std::vector<std::string> split(const std::string& s, char delimiter);
};
