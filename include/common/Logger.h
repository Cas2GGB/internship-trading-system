#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

// 简单的日志工具
// 未来改进：异步日志环形缓冲

enum class LogLevel {
    INFO,
    WARN,
    ERROR,
    DEBUG
};

class Logger {
public:
    static void log(LogLevel level, const std::string& module, const std::string& msg) {
        // 简单的同步日志，输出到 stderr
        // 格式: [TIMESTAMP] [LEVEL] [MODULE] MSG
        std::cerr << "[" << getCurrentTime() << "] "
                  << "[" << levelToString(level) << "] "
                  << "[" << module << "] " 
                  << msg << std::endl;
    }
    
    // 便捷方法
    static void info(const std::string& module, const std::string& msg) {
        log(LogLevel::INFO, module, msg);
    }
    
    static void error(const std::string& module, const std::string& msg) {
        log(LogLevel::ERROR, module, msg);
    }

private:
    static std::string getCurrentTime() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        // Add milliseconds? 
        return ss.str();
    }
    
    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::DEBUG: return "DEBUG";
            default: return "UNKNOWN";
        }
    }
};
