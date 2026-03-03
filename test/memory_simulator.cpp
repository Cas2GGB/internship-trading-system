#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <chrono>

// 模拟每次分配 100MB
const size_t CHUNK_SIZE = 100 * 1024 * 1024;
// 目标占用 2GB
const size_t TARGET_SIZE = 2UL * 1024 * 1024 * 1024;

int main() {
    std::vector<char*> allocated_chunks;
    size_t current_allocated = 0;

    std::cout << "Starting memory simulation..." << std::endl;
    std::cout << "Target: " << TARGET_SIZE / (1024 * 1024) << " MB" << std::endl;
    
    while (current_allocated < TARGET_SIZE) {
        try {
            // 分配内存
            char* chunk = new char[CHUNK_SIZE];
            
            // 重要：写入数据以触发缺页中断，确保物理内存真正被分配
            // 如果只分配不写入，Linux 的写时复制/延迟分配机制可能不会通过 top/ps 看到真实物理内存占用
            std::memset(chunk, 1, CHUNK_SIZE);
            
            allocated_chunks.push_back(chunk);
            current_allocated += CHUNK_SIZE;

            std::cout << "Allocated: " << current_allocated / (1024 * 1024) 
                      << " MB (Address: " << static_cast<void*>(chunk) << ")" << std::endl;

            // 稍微停顿一下，方便观察
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        } catch (const std::bad_alloc& e) {
            std::cerr << "Memory allocation failed: " << e.what() << std::endl;
            break;
        }
    }

    std::cout << "Memory allocation complete. Total allocated: " << current_allocated / (1024 * 1024) << " MB" << std::endl;
    std::cout << "Process PID: " << getpid() << std::endl;
    
    std::cout << "\nStarting delay detection monitor..." << std::endl;
    std::cout << "This process will now continuously check for execution pauses (e.g. caused by gcore)." << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    uint64_t counter = 0;

    // 无限循环监测
    while (true) {
        // 短暫睡眠，保持 CPU 占用极低，同时作为检测基准
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        
        // 正常情况下 elapsed_ms 应该稍微大于 10ms (例如 10-12ms)
        // 如果大大超过这个值（例如 > 50ms），说明进程被 OS 挂起了
        if (elapsed_ms > 50) {
             std::cout << "WARNING: Execution paused for " << elapsed_ms << " ms! (Possible snapshot in progress)" << std::endl;
        }

        last_time = now;
        
        // 每隔几秒输出一个心跳，证明程序还在运行
        if (++counter % 500 == 0) { // approx every 5 seconds
            std::cout << "Heartbeat: Process running normally..." << std::endl;
        }
    }

    // Unreachable code in this mode, but kept for completeness if loop condition changes
    // 清理内存
    for (char* chunk : allocated_chunks) {
        delete[] chunk;
    }

    return 0;
}
