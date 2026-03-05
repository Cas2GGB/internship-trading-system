#include "Engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>

Engine::Engine() {
}

Engine::~Engine() {
    // 销毁所有 OrderBook
    for(auto& pair : orderBooks) {
        delete pair.second;
    }
    orderBooks.clear();
}

void Engine::init(const std::string& configPath) {
    std::cout << "[Engine] Initialized support for multiple Stocks." << std::endl;
    std::cout << "[Engine] Loading config from " << configPath << std::endl;
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[Engine] Warning: Config file not found (" << configPath << "), skipping preload." << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        if (key == "INIT_STOCKS") {
             // Split value by comma
             std::stringstream ss(value);
             std::string segment;
             while(std::getline(ss, segment, ',')) {
                 try {
                     if (!segment.empty()) {
                         StockID stockId = std::stoull(segment);
                         getOrderBook(stockId);
                     }
                 } catch (...) {}
             }
        } else if (key == "SNAPSHOT_INTERVAL") {
             try {
                 snapshotInterval = std::stoull(value);
                 if (snapshotInterval > 0)
                     std::cout << "[Engine] Auto-snapshot interval set to " << snapshotInterval << " orders." << std::endl;
             } catch(...) {}
        }
    }
}

// 获取或惰性创建 OrderBook
OrderBook* Engine::getOrderBook(StockID stockId) {
    if (orderBooks.find(stockId) == orderBooks.end()) {
        orderBooks[stockId] = new OrderBook(stockId);
        std::cout << "[Engine] Created new OrderBook for Stock " << stockId << std::endl;
    }
    return orderBooks[stockId];
}

void Engine::run() {
    std::cout << "[Engine] Entering event loop. Type commands (e.g., ADD 1 100 10 1, SNAPSHOT)" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "EXIT") break;
        if (line.empty()) continue;
        
        processCommand(line);
        
        // 非阻塞检查子进程（快照工作进程）
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            std::cout << "[Engine] Snapshot process " << pid << " completed." << std::endl;
        }
    }
}

void Engine::loadScript(const std::string& scriptPath) {
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        std::cerr << "[Engine] Failed to open script: " << scriptPath << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        processCommand(line);
    }
}

void Engine::processCommand(const std::string& line) {
    auto tokens = split(line, ' ');
    if (tokens.empty()) return;
    
    std::string cmd = tokens[0];
    
    // Support comments
    if (cmd[0] == '#') return;
    
    if (cmd == "ADD") {
        // 扩展格式: ADD <StockID> <OrderID> <Price> <Qty> <Side 1=Buy/2=Sel> <Type>
        if (tokens.size() < 6) return;
        
        Order order;
        order.stockId = std::stoull(tokens[1]); // 新增 StockID 字段解析
        order.id = std::stoull(tokens[2]);
        order.price = std::stoi(tokens[3]);
        order.originalQty = std::stoull(tokens[4]);
        order.leavesQty = order.originalQty;
        order.side = (tokens[5] == "1" ? Side::BUY : Side::SELL);
        order.type = OrderType::LIMIT; // 默认限价单
        order.clientId = 1; // 默认客户ID
        
        // 简单的时间戳模拟
        // 在实际系统中，应使用 clock_gettime
        static uint64_t ts = 0;
        order.timestamp = ++ts;
        
        // 路由到对应的订单簿
        getOrderBook(order.stockId)->addOrder(order);
        
        if (snapshotInterval > 0 && ++processedOrderCount >= snapshotInterval) {
            triggerSnapshot();
            processedOrderCount = 0;
        }

    } else if (cmd == "CANCEL") {
        // 扩展格式: CANCEL <StockID> <OrderID>
        // 要撤单首先得知道这个单是属于哪个股票的，否则得扫描全部
        if (tokens.size() < 3) return;
        StockID stockId = std::stoull(tokens[1]);
        OrderID orderId = std::stoull(tokens[2]);
        
        OrderBook* ob = getOrderBook(stockId);
        bool success = ob->cancelOrder(orderId);
        
        if (!success) {
            std::cout << "[Engine] Cancel failed: Stock " << stockId << " Order " << orderId << " not found." << std::endl;
        }

        if (snapshotInterval > 0 && ++processedOrderCount >= snapshotInterval) {
            triggerSnapshot();
            processedOrderCount = 0;
        }
    } else if (cmd == "SNAPSHOT") {
        triggerSnapshot();
        
    } else if (cmd == "LOAD") {
         // LOAD <Filenane>
         // 加载时需要从文件名推断 StockID 或者文件头包含 StockID
         // 这里简单演示：假设所有快照都加载
         // TODO: 完善加载逻辑
         // orderBook->loadSnapshot(tokens[1]);
         std::cout << "[Engine] Load Snapshot not fully implemented for Multi-Stock yet!" << std::endl;
    }
}

void Engine::triggerSnapshot() {
    auto start = std::chrono::high_resolution_clock::now();
    
    pid_t pid = fork();
    
    if (pid < 0) {
        std::cerr << "[Engine] Fork failed!" << std::endl;
    } else if (pid == 0) {
        // 子进程
        // 遍历所有 OrderBook 并保存
        for (auto& pair : orderBooks) {
            StockID sid = pair.first;
            OrderBook* ob = pair.second;
            
            std::string filename = "snapshot_stock_" + std::to_string(sid) + "_" + std::to_string(getpid()) + ".dat";
            ob->saveSnapshot(filename);
            
            // 可以打印一下
            // std::cout << "Saved snapshot " << filename << std::endl;
        }
        
        _exit(0); 
    } else {
        // 父进程
        // 立即返回以继续处理订单
        auto end = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "[Engine] Snapshot triggered. Fork time: " << diff.count() << " us. Child PID: " << pid << std::endl;
    }
}

std::vector<std::string> Engine::split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}
