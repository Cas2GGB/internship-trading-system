#include "Engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <chrono>

bool g_isStressTest = false;

Engine::Engine() {
    // Allow any process to ptrace this process (needed for gcore in some environments)
    // This is useful for debugging or external snapshots without root.
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
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
                         supportedStocks.insert(stockId);
                     }
                 } catch (...) {}
             }
             std::cout << "[Engine] Configured " << supportedStocks.size() << " supported stocks." << std::endl;
        } else if (key == "SNAPSHOT_INTERVAL") {
             try {
                 snapshotInterval = std::stoull(value);
                 if (snapshotInterval > 0)
                     std::cout << "[Engine] Auto-snapshot interval set to " << snapshotInterval << " orders." << std::endl;
             } catch(...) {}
        } else if (key == "ENABLE_SNAPSHOT") {
             // 0=Disable, 1=Enable
             try {
                 int val = std::stoi(value);
                 enableSnapshot = (val != 0);
             } catch(...) {}
        } else if (key == "ENABLE_STRESS_TEST") {
             try {
                 int val = std::stoi(value);
                 g_isStressTest = (val != 0);
                 if (g_isStressTest) std::cout << "[Engine] Stress Test Mode ENABLED. Verbose logs muted." << std::endl;
             } catch(...) {}
        }
    }
}

// 获取或惰性创建 OrderBook
OrderBook* Engine::getOrderBook(StockID stockId) {
    // 如果配置了白名单，且该股票不在白名单内，则拒绝创建
    if (!supportedStocks.empty() && supportedStocks.find(stockId) == supportedStocks.end()) {
        return nullptr;
    }

    if (orderBooks.find(stockId) == orderBooks.end()) {
        orderBooks[stockId] = new OrderBook(stockId, &accountManager);
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
        
        // Remove waitpid to avoid system call overhead in tight loop
        // The child process will exit on its own.
        // In a real production system, we would handle SIGCHLD signal asynchronously.
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
    auto start = std::chrono::high_resolution_clock::now(); // Start Timer
    
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
        order.clientId = (tokens.size() >= 7) ? std::stoull(tokens[6]) : 1; // 客户ID，默认1
        
        // 增加网关层的资金校验 (Risk Check)
        Account& acc = accountManager.getAccount(order.clientId);
        if (g_isStressTest) { /* 压测模式下忽略资金持仓校验以方便生成无限单 */ } else if (order.side == Side::BUY) {
            Price cost = order.price * order.originalQty;
            if (acc.balance < cost) {
                if (!g_isStressTest) std::cout << "[Engine] 拒单 (资金不足): Client " << order.clientId << " Balance " << acc.balance << " < Cost " << cost << std::endl;
                return;
            }
            acc.balance -= cost;
            acc.frozenFunds += cost;
        } else {
            if (acc.positions[order.stockId] < order.originalQty) {
                if (!g_isStressTest) std::cout << "[Engine] 拒单 (持仓不足): Client " << order.clientId << " Position " << acc.positions[order.stockId] << " < Qty " << order.originalQty << std::endl;
                return;
            }
            acc.positions[order.stockId] -= order.originalQty;
            acc.frozenPositions[order.stockId] += order.originalQty;
        }

        // 简单的时间戳模拟
        // 在实际系统中，应使用 clock_gettime
        static uint64_t ts = 0;
        order.timestamp = ++ts;
        
        // 路由到对应的订单簿
        OrderBook* ob = getOrderBook(order.stockId);
        if (ob) {
            ob->addOrder(order);
        } else {
            std::cerr << "[Engine] ADD Error: Stock " << order.stockId << " is not supported." << std::endl;
            return;
        }
        
        if (enableSnapshot && snapshotInterval > 0 && ++processedOrderCount >= snapshotInterval) {
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
        if (!ob) {
            std::cerr << "[Engine] CANCEL Error: Stock " << stockId << " is not supported." << std::endl;
            return;
        }

        bool success = ob->cancelOrder(orderId);
        
        if (!g_isStressTest) {
            if (!success) {
                std::cout << "[Engine] 撤单失败: Stock " << stockId << " Order " << orderId << " (已完全成交或订单不存在)" << std::endl;
            } else {
                std::cout << "[Engine] 撤单成功: Stock " << stockId << " Order " << orderId << " (已撤销剩余未成交部分)" << std::endl;
            }
        }

        if (enableSnapshot && snapshotInterval > 0 && ++processedOrderCount >= snapshotInterval) {
            triggerSnapshot();
            processedOrderCount = 0;
        }
    } else if (cmd == "SNAPSHOT") {
        if (enableSnapshot) triggerSnapshot();
    } else if (cmd == "RESTORE") {
        // 命令格式: RESTORE (一键恢复所有股票字典和账户)
        
        // 1. 恢复统管的账户状态
        accountManager.loadSnapshot("test/snapshot_accounts.dat");
        std::cout << "[Engine] Global Restore: Accounts loaded." << std::endl;

        // 2. 遍历我们当时保存的快照。目前的架构下我们依赖于预先配置了多少个支持的股票，或者可以暴力扫描当前目录
        // 为了简化，我们遍历当前引擎配置里支持的所有的股票 ID 尝试加载对应的文件
        int restoredCount = 0;
        for (const auto& sid : supportedStocks) {
            std::string filename = "test/snapshot_" + std::to_string(sid) + ".dat";
            // 检查文件是否存在
            if (access(filename.c_str(), F_OK) != -1) {
                OrderBook* ob = getOrderBook(sid);
                if (ob) {
                    ob->loadSnapshot(filename);
                    restoredCount++;
                    std::cout << "[Engine] Global Restore: OrderBook for Stock " << sid << " loaded." << std::endl;
                }
            }
        }
        
        if (restoredCount == 0 && supportedStocks.empty()) {
             // 容错：如果没有配置白名单，我们可以强制指定加载测试用的股票 1
             std::string filename = "test/snapshot_1.dat";
             if (access(filename.c_str(), F_OK) != -1) {
                 OrderBook* ob = getOrderBook(1);
                 if (ob) {
                     ob->loadSnapshot(filename);
                     std::cout << "[Engine] Global Restore: OrderBook for Stock 1 loaded (Fallback)." << std::endl;
                 }
             }
        }
        std::cout << "[Engine] System Global Restore Completed!" << std::endl;
        
    } else if (cmd == "SLEEP") {
        // 用于给外部 gcore 争取时间，防止极小数据集瞬间跑完
        if (tokens.size() < 2) return;
        int ms = std::stoi(tokens[1]);
        std::cout << "[Engine] Sleeing for " << ms << " ms (Simulating blocking/Wait for Gcore)..." << std::endl;
        usleep(ms * 1000);
    } else if (cmd == "ACCOUNT") {
        // 用于打印账户信息 ACCOUNT <ClientID>
        if (tokens.size() < 2) return;
        ClientID cid = std::stoull(tokens[1]);
        Account& acc = accountManager.getAccount(cid);
        std::cout << "[Engine] Account " << cid << " | Balance: " << acc.balance 
                  << " | FrozenFunds: " << acc.frozenFunds;
        // 简单打印一下当前股票 1 的持仓
        std::cout << " | Pos(Stock 1): " << acc.positions[1] 
                  << " | FrozenPos(Stock 1): " << acc.frozenPositions[1] << std::endl;
    } else if (cmd == "GIVE_POS") {
        // 用于在测试数据开头强行给空头分配仓位 GIVE_POS <ClientID> <StockID> <Qty>
        if (tokens.size() < 4) return;
        ClientID cid = std::stoull(tokens[1]);
        StockID sid = std::stoull(tokens[2]);
        Qty q = std::stoull(tokens[3]);
        Account& acc = accountManager.getAccount(cid);
        acc.positions[sid] += q;
        std::cout << "[Engine] GIVE_POS: Client " << cid << " received " << q << " units of Stock " << sid << std::endl;
    }
        
    // 性能指标
    auto end = std::chrono::high_resolution_clock::now();
    // 之前只计算 end - start (执行时间)，如果此时正好发生了 gcore/fork 卡顿但卡在 getline 等待上，
    // 则该命令的执行时间依然很短，导致图形中偶尔抓不到 max latency 尖刺。
    // 因此我们引入 cycle_latency (端到端循环时间) 来捕捉系统的一切阻塞。
    static auto lastEnd = start;
    auto cycle_latency = std::chrono::duration_cast<std::chrono::microseconds>(end - lastEnd).count();
    lastEnd = end;

    static uint64_t totalLatency = 0;
    static uint64_t maxLatency = 0;
    static uint64_t processedOrders = 0;
    // 使用第一条有效命令的 开始时间(start) 作为基准，而不是当前时间(now)，以包含第一条单的耗时
    static auto startTime = start; 

    if (cmd == "RESET_METRICS") {
        totalLatency = 0;
        maxLatency = 0;
        processedOrders = 0;
        startTime = std::chrono::high_resolution_clock::now();
        lastEnd = startTime;
        std::cout << "[Engine] Metrics reset. Ready for Phase 2." << std::endl;
        
        // Print memory usage
        std::ifstream statusFile("/proc/self/status");
        std::string statusLine;
        while (std::getline(statusFile, statusLine)) {
            if (statusLine.find("VmRSS:") != std::string::npos) {
                std::cout << "[Engine] Current Memory Usage: " << statusLine << std::endl;
                break;
            }
        }
        return; // Don't count this command in stats
    }
    
    // 记录所有命令的循环耗时，彻底捕捉任何被 OS 强行暂停的真空时间
    processedOrders++;
    totalLatency += cycle_latency;
    if (cycle_latency > maxLatency) maxLatency = cycle_latency;
    
    // 每 10万条订单报告一次
    if (processedOrders % 100000 == 0) {
        auto now = std::chrono::high_resolution_clock::now();
        // 使用微秒计算以避免秒级取整导致的 TPS 误差（例如 1.9秒被记为 1秒）
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime).count();
        if (durationUs > 0) {
            double tps = (processedOrders * 1000000.0) / (double)durationUs;
            double avgLat = totalLatency / (double)processedOrders;
            std::cout << "[Perf] Orders: " << processedOrders 
                      << " | TPS: " << (uint64_t)tps 
                      << " | Avg Latency: " << avgLat << " us"
                          << " | Max Latency: " << maxLatency << " us" << std::endl;
        }
    }
}

void Engine::triggerSnapshot() {
    auto start = std::chrono::high_resolution_clock::now();
    
    pid_t pid = fork();
    
    if (pid < 0) {
        std::cerr << "[Engine] Fork failed!" << std::endl;
    } else if (pid == 0) {
        // 子进程
        
        // 1. 保存账户信息汇总
        accountManager.saveSnapshot("test/snapshot_accounts.dat");

        // 2. 遍历所有 OrderBook 并保存
        for (auto& pair : orderBooks) {
            StockID sid = pair.first;
            OrderBook* ob = pair.second;
            
            // 固定名字，只留一个快照以供复盘加载测试使用
            std::string filename = "test/snapshot_" + std::to_string(sid) + ".dat";
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
