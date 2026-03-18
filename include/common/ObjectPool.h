#pragma once
#include <vector>
#include <memory>
#include <cassert>
#include <new>

/**
 * @brief ObjectPool 内存池模版类
 *
 * 采用 Block 分段 + 侵入式空闲链表（FreeList）机制按需管理内存。
 * 系统不在启动时预分配所有内存，而是在当前 Block 用尽时才申请新的
 * 固定大小 Block（默认每块 BlockSize 个 Slot），从而在峰值订单数
 * 远大于均值的场景下避免启动时的巨量内存占用。
 *
 * 内存布局：
 *   - blocks[]：按需增长的 Block 列表，每个 Block 内部是连续 Slot 数组
 *   - freeHead：空闲链表头，归还的对象通过头插法链入此处
 *   - nextIndexInCurrentBlock：当前 Block 内下一个可用 Slot 的下标
 *
 * 分配优先级：
 *   1. 从空闲链表（freeHead）复用已归还的 Slot
 *   2. 从当前 Block 的 nextIndex 顺序取出新 Slot
 *   3. 当前 Block 用尽时，申请新 Block（唯一触发堆分配的时机）
 *
 * 与 fork COW 的协同：
 *   Block 内部的 Slot 是连续的，同一 Block 的订单对象 Cache 友好。
 *   相比散乱的 new/delete，Block 化管理显著减少内存碎片，
 *   fork 时页表条目更规整，COW 触发范围更集中。
 */
template <typename T, size_t BlockSize = 4096>
class ObjectPool {
private:
    // 联合体复用同一块内存：
    //   对象存活时：存放类型 T 的数据
    //   对象释放后：存放指向下一个空闲 Slot 的 next 指针
    union Slot {
        T     element;
        Slot* next;

        Slot()  {} // 不做任何构造，由 acquire 通过 placement new 负责
        ~Slot() {} // 不做任何析构，由 release 显式调用 T::~T() 负责
    };

    struct Block {
        Slot slots[BlockSize]; // Block 内连续的 Slot 数组
    };

    std::vector<std::unique_ptr<Block>> blocks; // 持有所有 Block 的生命周期
    Slot*  freeHead;                            // 空闲链表头指针
    size_t nextIndexInCurrentBlock;             // 当前 Block 内下一个可用 Slot 的下标

public:
    /**
     * @brief 构造时预分配 preAllocBlocks 个 Block，后续按需继续扩容。
     * @param preAllocBlocks 预分配的 Block 数量，默认预热 1 个 Block。
     *        例如：已知峰值约 800 万订单，BlockSize=4096，
     *        则预分配 800万/4096 ≈ 2000 个 Block 可完全避免运行期扩容。
     *        保守起见默认只预热 1 个，避免启动时内存暴涨。
     */
    explicit ObjectPool(size_t preAllocBlocks = 1)
        : freeHead(nullptr), nextIndexInCurrentBlock(BlockSize) {
        blocks.reserve(preAllocBlocks);
        for (size_t i = 0; i < preAllocBlocks; ++i) {
            allocateBlock();
        }
    }

    ~ObjectPool() {
        // Block 由 unique_ptr 管理，自动释放
        // 假设所有者（OrderBook）在析构前已主动 release 了全部对象
    }

    // 禁止拷贝，避免 Block 所有权混乱
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /**
     * @brief 从池中取出一个 Slot，placement new 构造对象后返回指针。
     *        O(1) 摊销，仅在 Block 耗尽时触发一次堆分配。
     */
    template <typename... Args>
    T* acquire(Args&&... args) {
        Slot* slot = nullptr;

        // 1. 优先从空闲链表复用已归还的 Slot
        if (freeHead) {
            slot    = freeHead;
            freeHead = freeHead->next;
        }
        // 2. 从当前 Block 顺序分配
        else {
            if (nextIndexInCurrentBlock >= BlockSize) {
                allocateBlock(); // 按需申请新 Block
            }
            slot = &(blocks.back()->slots[nextIndexInCurrentBlock++]);
        }

        // placement new：在已有内存上构造对象，不触发堆分配
        return new (&slot->element) T(std::forward<Args>(args)...);
    }

    /**
     * @brief 归还对象：显式析构后，将 Slot 头插回空闲链表。O(1)。
     */
    void release(T* ptr) {
        if (!ptr) return;

        // 1. 显式调用析构函数（不释放内存，仅清理对象内部资源）
        ptr->~T();

        // 2. 将该 Slot 头插到空闲链表
        Slot* slot  = reinterpret_cast<Slot*>(ptr);
        slot->next  = freeHead;
        freeHead    = slot;
    }

    // 当前已分配的 Block 数量
    size_t blockCount() const { return blocks.size(); }

private:
    void allocateBlock() {
        blocks.push_back(std::make_unique<Block>());
        nextIndexInCurrentBlock = 0;
    }
};
