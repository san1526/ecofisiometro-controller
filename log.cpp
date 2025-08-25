#include "log.hpp"
#include "shorthand.hpp"

static std::vector<LogEntry> s_logs;

const std::vector<LogEntry> &get_log_lines()
{
    return s_logs;
}

void log_impl(
    LogContext ctx, std::string_view file, std::string_view func, int line, LogSeverity severity, std::string &&msg)
{
    // TODO remove this line?
    fprintf(stderr, "[%.*s:%d] %s\n", (u32)func.size(), func.data(), line, msg.c_str());

    s_logs.emplace_back(std::string{file},
                        std::string{func},
                        line,
                        ctx,
                        severity,
                        std::chrono::system_clock::now(),
                        std::forward<std::string &&>(msg));
}
