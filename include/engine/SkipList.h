#pragma once

#include <vector>
#include <random>
#include <ctime>
#include <iostream>
#include "PriceLevel.h"

// 定义跳表的最大层级
const int MAX_LEVEL = 16;
const float P = 0.5f;

// -----------------------------------------------------------------------------
// 比较器仿函数
// -----------------------------------------------------------------------------

struct Less {
    bool operator()(const Price& a, const Price& b) const {
        return a < b;
    }
};

struct Greater {
    bool operator()(const Price& a, const Price& b) const {
        return a > b;
    }
};

/**
 * @brief SkipList 跳表模版
 * 
 * 专为订单簿价格档位设计的简化版跳表。
 * 支持 O(log N) 的插入、删除和查找。
 * 
 * 模版参数:
 * - Comparator: 严格弱序仿函数 (Less 用于卖单, Greater 用于买单)
 */
template <typename Comparator>
class SkipList {
public:
    SkipList() : level(0) {
        // 创建哑头节点 (dummy head)。
        // 对于 'Less' (卖单 - 升序)，我们希望 head 小于任何有效价格。
        // 对于 'Greater' (买单 - 降序)，我们希望 head 大于任何有效价格。
        // 通常的做法是使用 max/min 极限值，或者在逻辑上特殊处理 head。
        // 这里我们简单使用 0 作为哨兵值，遍历逻辑会跳过检查 head 的值。
        head = new PriceLevel(0); 
        for(int i=0; i<MAX_LEVEL; ++i) head->forward[i] = nullptr;
        
        // 随机种子
        std::srand(std::time(nullptr));
    }
    
    ~SkipList() {
        PriceLevel* curr = head;
        while(curr) {
            PriceLevel* next = curr->forward[0];
            delete curr;
            curr = next;
        }
    }

    // 插入或查找已存在的 PriceLevel
    PriceLevel* insert(Price price) {
        PriceLevel* update[MAX_LEVEL];
        PriceLevel* curr = head;
        
        // 标准跳表插入逻辑
        for (int i = level; i >= 0; i--) {
            // 当下一个节点的价格 "小于" 目标价格时（根据 Comparator），继续前进
            while (curr->forward[i] && comp(curr->forward[i]->price, price)) {
                curr = curr->forward[i];
            }
            update[i] = curr;
        }
        
        // 移动到第 0 层的大于等于目标的节点
        curr = curr->forward[0];
        
        // 如果键已存在，直接返回
        // 注意：对于 'Less'，我们停在 next >= price。但刚才只检查了 <。
        // 所以这里需要检查相等。
        // 对于 'Greater'，我们停在 next <= price。同理。
        if (curr && curr->price == price) {
            return curr;
        }
        
        // 插入新节点
        int lv = randomLevel();
        if (lv > level) {
            for (int i = level + 1; i <= lv; i++) {
                update[i] = head;
            }
            level = lv;
        }
        
        PriceLevel* newNode = new PriceLevel(price);
        for (int i = 0; i <= lv; i++) {
            newNode->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = newNode;
        }
        
        return newNode;
    }

    // 如果价格档位为空（无订单），则移除该节点
    // 返回 true 表示已移除
    bool removeIfEmpty(PriceLevel* node) {
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
            for (int i = 0; i <= level; i++) {
                if (update[i]->forward[i] != curr) break;
                update[i]->forward[i] = curr->forward[i];
            }
            delete curr;
            
            // 调整当前最大层级
            while (level > 0 && head->forward[level] == nullptr) {
                level--;
            }
            return true;
        }
        return false;
    }
    
    // 获取第一个有效节点（最优买单/最优卖单）
    PriceLevel* begin() const {
        return head->forward[0];
    }
    
    // 检查列表是否为空
    bool empty() const {
        return head->forward[0] == nullptr;
    }

private:
    int level;
    PriceLevel* head;
    Comparator comp;

    int randomLevel() {
        int lvl = 0;
        while ((float)std::rand()/RAND_MAX < P && lvl < MAX_LEVEL - 1) {
            lvl++;
        }
        return lvl;
    }
};
