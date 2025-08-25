#pragma once

// TOCO this logger is ASS
#include "shorthand.hpp"
#include <chrono>
#include <string>
#include <vector>

using TimeStamp = std::chrono::time_point<std::chrono::system_clock>;

enum class LogSeverity : u8 {
    DEBUG = 0,
    NORM = 1,
    ERROR_ = 2,

    MAX = ERROR_,
};

enum class LogContext {
    APP,
    DEVICE,
};

struct LogEntry {
    std::string file;
    std::string function;
    int line;
    LogContext context;
    LogSeverity severity;
    TimeStamp ts;
    std::string msg;
};

void log_impl(
    LogContext ctx, std::string_view file, std::string_view func, int line, LogSeverity severity, std::string &&msg);
const std::vector<LogEntry> &get_log_lines();

#define LOG_DEBUG(msg, ...) \
    log_impl(LogContext::APP, __FILE__, __FUNCTION__, __LINE__, LogSeverity::DEBUG, std::format(msg, __VA_ARGS__));
#define LOG_NORM(msg, ...) \
    log_impl(LogContext::APP, __FILE__, __FUNCTION__, __LINE__, LogSeverity::NORM, std::format(msg, __VA_ARGS__));
#define LOG_ERROR(msg, ...) \
    log_impl(LogContext::APP, __FILE__, __FUNCTION__, __LINE__, LogSeverity::ERROR_, std::format(msg, __VA_ARGS__));
