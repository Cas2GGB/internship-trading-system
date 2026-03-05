#pragma once
#include <vector>
#include <memory>
#include <cassert>
#include <new>

/**
 * @brief ObjectPool 内存池模版类
 * 
 * 用于高频创建和销毁的对象（如 Order），通过预分配大块内存（Block）
 * 和维护空闲链表（FreeList）来减少堆内存分配开销和内存碎片。
 */
template <typename T, size_t BlockSize = 4096>
class ObjectPool {
private:
    // 联合体保证内存对齐，并重用对象 T 和 next 指针的内存
    union Slot {
        T element;
        Slot* next;

        Slot() {} // 默认构造函数不做任何事
        ~Slot() {} // 析构函数不做任何事
    };

    struct Block {
        Slot slots[BlockSize];
    };

    std::vector<std::unique_ptr<Block>> blocks;
    Slot* freeHead;
    size_t nextIndexInCurrentBlock;

public:
    ObjectPool() : freeHead(nullptr), nextIndexInCurrentBlock(BlockSize) {
        // 初始为空，第一次 acquire 时分配内存块
    }

    ~ObjectPool() {
        // Block 由 unique_ptr 管理，会自动释放
        // 假设对象不需要除了内存释放以外的显式析构逻辑
        // 或者所有者 (OrderBook) 已经主动释放了它们
    }

    // 分配对象并构造
    template <typename... Args>
    T* acquire(Args&&... args) {
        Slot* slot = nullptr;

        // 1. 尝试从空闲链表中重用
        if (freeHead) {
            slot = freeHead;
            freeHead = freeHead->next;
        } 
        // 2. 从当前内存块分配
        else {
            if (nextIndexInCurrentBlock >= BlockSize) {
                allocateBlock();
            }
            slot = &(blocks.back()->slots[nextIndexInCurrentBlock++]);
        }

        // 3. 使用 placement new 构造对象
        T* ptr = &(slot->element);
        return new (ptr) T(std::forward<Args>(args)...);
    }

    // 将对象归还给对象池
    void release(T* ptr) {
        if (!ptr) return;

        // 1. 析构
        ptr->~T();

        // 2. 加入空闲链表 (头插法)
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        slot->next = freeHead;
        freeHead = slot;
    }

private:
    void allocateBlock() {
        blocks.push_back(std::make_unique<Block>());
        nextIndexInCurrentBlock = 0;
    }
};
