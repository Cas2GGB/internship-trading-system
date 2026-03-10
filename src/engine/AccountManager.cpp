#include "AccountManager.h"
#include <fstream>
#include <iostream>
#include <vector>

Account& AccountManager::getAccount(ClientID id) {
    auto it = accounts.find(id);
    if (it == accounts.end()) {
        accounts[id].id = id;
        accounts[id].balance = 100000000; // 默认发一亿
        return accounts[id];
    }
    return it->second;
}

void AccountManager::saveSnapshot(const std::string& filename) {
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return;

    uint64_t count = accounts.size();
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // 使用内存缓冲批量写入，代替繁琐的单条 write() 系统调用
    std::vector<char> buffer;
    // 预估大小: 每个账户基础信息约占用 24 字节 + 浮动仓位大小，先 reserve 减少扩容
    buffer.reserve(count * 64); 

    for (auto& pair : accounts) {
        Account& acc = pair.second;
        
        // 核心信息打包追加
        const char* pId = reinterpret_cast<const char*>(&acc.id);
        buffer.insert(buffer.end(), pId, pId + sizeof(acc.id));
        
        const char* pBal = reinterpret_cast<const char*>(&acc.balance);
        buffer.insert(buffer.end(), pBal, pBal + sizeof(acc.balance));
        
        const char* pFroz = reinterpret_cast<const char*>(&acc.frozenFunds);
        buffer.insert(buffer.end(), pFroz, pFroz + sizeof(acc.frozenFunds));
        
        // 可用持仓信息打包追加
        uint64_t posCount = acc.positions.size();
        const char* pPosC = reinterpret_cast<const char*>(&posCount);
        buffer.insert(buffer.end(), pPosC, pPosC + sizeof(posCount));
        
        for (auto& pp : acc.positions) {
            const char* pFirst = reinterpret_cast<const char*>(&pp.first);
            buffer.insert(buffer.end(), pFirst, pFirst + sizeof(pp.first));
            
            const char* pSec = reinterpret_cast<const char*>(&pp.second);
            buffer.insert(buffer.end(), pSec, pSec + sizeof(pp.second));
        }

        // 冻结持仓信息打包追加
        uint64_t fPosCount = acc.frozenPositions.size();
        const char* pFposC = reinterpret_cast<const char*>(&fPosCount);
        buffer.insert(buffer.end(), pFposC, pFposC + sizeof(fPosCount));
        
        for (auto& pp : acc.frozenPositions) {
            const char* pFirst = reinterpret_cast<const char*>(&pp.first);
            buffer.insert(buffer.end(), pFirst, pFirst + sizeof(pp.first));
            
            const char* pSec = reinterpret_cast<const char*>(&pp.second);
            buffer.insert(buffer.end(), pSec, pSec + sizeof(pp.second));
        }
        
        // 如果遇到了超大规模客户数，也做一下 10MB 分块刷盘控制避免内存刺穿
        if (buffer.size() > 10 * 1024 * 1024) {
            ofs.write(buffer.data(), buffer.size());
            buffer.clear();
        }
    }
    
    // 写入最后剩余的缓冲区
    if (!buffer.empty()) {
        ofs.write(buffer.data(), buffer.size());
    }
}

void AccountManager::loadSnapshot(const std::string& filename) {
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs) return;

    // 获取文件总大小
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    accounts.clear();
    uint64_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    // 计算剩余数据大小并一次性全部读入内存 (通常账户数据不大，直接全读是最快的)
    std::streamsize remainingSize = size - sizeof(count);
    if (remainingSize <= 0) return;

    std::vector<char> buffer(remainingSize);
    if (!ifs.read(buffer.data(), remainingSize)) return;

    // 解析内存中的二进制流
    const char* ptr = buffer.data();
    const char* end = buffer.data() + remainingSize;

    for (uint64_t i = 0; i < count; ++i) {
        if (ptr >= end) break;
        
        Account acc;
        acc.id = *reinterpret_cast<const ClientID*>(ptr); ptr += sizeof(ClientID);
        acc.balance = *reinterpret_cast<const Price*>(ptr); ptr += sizeof(Price);
        acc.frozenFunds = *reinterpret_cast<const Price*>(ptr); ptr += sizeof(Price);
        
        uint64_t posCount = *reinterpret_cast<const uint64_t*>(ptr); ptr += sizeof(uint64_t);
        for (uint64_t j = 0; j < posCount; ++j) {
            StockID s = *reinterpret_cast<const StockID*>(ptr); ptr += sizeof(StockID);
            Qty q = *reinterpret_cast<const Qty*>(ptr); ptr += sizeof(Qty);
            acc.positions[s] = q;
        }

        uint64_t fPosCount = *reinterpret_cast<const uint64_t*>(ptr); ptr += sizeof(uint64_t);
        for (uint64_t j = 0; j < fPosCount; ++j) {
            StockID s = *reinterpret_cast<const StockID*>(ptr); ptr += sizeof(StockID);
            Qty q = *reinterpret_cast<const Qty*>(ptr); ptr += sizeof(Qty);
            acc.frozenPositions[s] = q;
        }
        accounts[acc.id] = acc;
    }
}
