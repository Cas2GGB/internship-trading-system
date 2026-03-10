#pragma once
#include <unordered_map>
#include <string>
#include "Account.h"

/**
 * @brief 账户体系管理器类
 * 管理所有的用户账户信息，提供账户查询与持久化快照保存/加载的支持。
 */
class AccountManager {
public:
    // 存储所有用户账户，按客户ID进行散列索引
    std::unordered_map<ClientID, Account> accounts;

    /**
     * @brief 获取指定客户端ID的账户信息。
     * 如果账户不存在则会隐式创建，并赋予默认初始资金。
     * @param id 客户ID
     * @return 返回该客户的账户引用
     */
    Account& getAccount(ClientID id);

    /**
     * @brief 将当前内存中所有用户的资金和持仓状态持久化（保存快照）到磁盘文件中。
     * @param filename 要保存的二进制快照文件路径
     */
    void saveSnapshot(const std::string& filename);

    /**
     * @brief 从快照文件中反序列化并还原系统所有用户的账户状态。
     * @param filename 快照文件路径
     */
    void loadSnapshot(const std::string& filename);
};
