#pragma once

#include <vector>
#include <random>
#include <ctime>
#include <iostream>
#include "PriceLevel.h"

// 定义跳表的最大高度层级，限制最大层级可避免动态内存分配同时提供极好的查找性能
const int MAX_LEVEL = 16;
// 向上攀升层级的概率，0.5代表抛硬币一样的概率
const float P = 0.5f;

// -----------------------------------------------------------------------------
// 价格比较器仿函数
// 用于指导跳表在水平方向的排序规则 (卖单要找最低价，买单要找最高价)
// -----------------------------------------------------------------------------

/**
 * @brief 卖单优先排序比较器 (升序)
 * 让跳表的节点始终维护从小到大排列。对应寻找最优低价卖出档位
 */
struct Less {
    bool operator()(const Price& a, const Price& b) const {
        return a < b;
    }
};

/**
 * @brief 买单优先排序比较器 (降序)
 * 让跳表的节点始终维护从大到小排列。对应寻找最优高价买入档位
 */
struct Greater {
    bool operator()(const Price& a, const Price& b) const {
        return a > b;
    }
};

/**
 * @brief SkipList 高性能并发跳表模版
 * 
 * 专为引擎内存订单簿(OrderBook)价格档位设计的简化版跳表结构。
 * 它能够替代常见的红黑树(std::map)，提供平均 O(log N) 时间复杂度的插入、删除和查询。
 * 在做序列化快照和重建时，无需平衡树节点，结构重建速度极快。
 * 
 * 模版参数:
 * - Comparator: 严格弱序仿函数 (Less 用于卖方盘口排序, Greater 用于买方盘口排序)
 */
template <typename Comparator>
class SkipList {
public:
    /**
     * @brief 构造函数，初始化哨兵头节点并设置随机种子
     */
    SkipList() : level(0) {
        // 创建哑头节点 (dummy head)。
        // 它的价格值在这里我们赋予极值0作为占位哨兵。
        // 在实际遍历搜寻时，算法设计会自动利用前向指针跨过它。
        head = new PriceLevel(0); 
        for(int i=0; i<MAX_LEVEL; ++i) head->forward[i] = nullptr;
        
        std::srand(std::time(nullptr));
    }
    
    /**
     * @brief 析构函数，安全释放掉所有当前挂载的价格档位节点内存
     */
    ~SkipList() {
        PriceLevel* curr = head;
        while(curr) {
            PriceLevel* next = curr->forward[0];
            delete curr;
            curr = next;
        }
    }

    /**
     * @brief 在跳表中插入并定位一个指定的档位价格，若已经存在则直接返回
     * @param price 需要插入新设档位的具体价格
     * @return 返回插入或已有的 PriceLevel 结构体节点指针
     */
    PriceLevel* insert(Price price) {
        PriceLevel* update[MAX_LEVEL];
        PriceLevel* curr = head;
        
        // 从最高层自顶向下开始搜索位置
        for (int i = level; i >= 0; i--) {
            // 根据比较器策略遍历，直至下一个节点不再“更优”
            while (curr->forward[i] && comp(curr->forward[i]->price, price)) {
                curr = curr->forward[i];
            }
            // 记录下每层需要更新指针跨度的源节点
            update[i] = curr;
        }
        
        // 移步到底层0级的前置位点
        curr = curr->forward[0];
        
        // 如果该价格节点已经在此前被建立过了，则复用该层节点将其抛出
        if (curr && curr->price == price) {
            return curr;
        }
        
        // 若没有找到，则抛硬币随机决定一个新节点应占据的索引高度
        int lv = randomLevel();
        if (lv > level) {
            for (int i = level + 1; i <= lv; i++) {
                update[i] = head;
            }
            level = lv;
        }
        
        // 在内存里组装新的价格并串联四通八达的指针网
        PriceLevel* newNode = new PriceLevel(price);
        for (int i = 0; i <= lv; i++) {
            newNode->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = newNode;
        }
        
        return newNode;
    }

    /**
     * @brief (惰性删除) 试图移除一个不再保留任何剩余订单的价格档位
     * @param node 要求被检测移除的目标价格节点
     * @return 若确实该节点空了并顺带完成了斩断内存与删除工作，返回 true
     */
    bool removeIfEmpty(PriceLevel* node) {
        // 如果本节点仍下挂着等待撮合的活跃订单，则终止清理，保留结构不变
        if (!node->isEmpty()) return false;
        
        PriceLevel* update[MAX_LEVEL];
        PriceLevel* curr = head;
        Price price = node->price;
        
        for (int i = level; i >= 0; i--) {
            while (curr->forward[i] && comp(curr->forward[i]->price, price)) {
                curr = curr->forward[i];
            }
            update[i] = curr;
        }

        curr = curr->forward[0];
        
        if (curr == node) {
            // 执行指针跃接切分，把它从跳表的经络网络里摘掉
            for (int i = 0; i <= level; i++) {
                if (update[i]->forward[i] != curr) break;
                update[i]->forward[i] = curr->forward[i];
            }
            delete curr;
            
            // 全局高度收缩校准（如删掉的是碰巧唯一一个最高索引层）
            while (level > 0 && head->forward[level] == nullptr) {
                level--;
            }
            return true;
        }
        return false;
    }
    
    /**
     * @brief 获取当前挂单的最优档位 (即头结点的第一层出水点，包含最佳买或最佳卖价)
     */
    PriceLevel* begin() const {
        return head->forward[0];
    }
    
    /**
     * @brief 当前是否没有任何有效档位挂单了
     */
    bool empty() const {
        return head->forward[0] == nullptr;
    }

private:
    int level;               // 现阶段跳表真实达到的最大高度层级
    PriceLevel* head;        // 跳表的固定物理切入锚点头：一切寻址起始于此
    Comparator comp;         // 维护着本跳表排序灵魂规则的实例对象

    /**
     * @brief 用掷硬币模拟法来决断每个新节点能往上“长”多少层，返回生成的高度数
     */
    int randomLevel() {
        int lvl = 0;
        // P为0.5代表大概率大量节点只配具有0层、1层索引，极少数幸运儿拥有极高跨层视野，以达到平衡树般的搜索时间
        while ((float)std::rand()/RAND_MAX < P && lvl < MAX_LEVEL - 1) {
            lvl++;
        }
        return lvl;
    }
};
