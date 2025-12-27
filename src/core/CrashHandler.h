#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <functional>

namespace Core {

// Log levels for the engine logger
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

// A single log entry
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string category;
    std::string message;
    std::string file;
    int line;
};

// Engine Logger - tracks what the engine is doing
// Keeps a rolling buffer of recent entries for crash reports
class Logger {
public:
    static Logger& instance();

    // Log a message
    void log(LogLevel level, const std::string& category, const std::string& message,
             const char* file = nullptr, int line = 0);

    // Convenience methods
    void debug(const std::string& category, const std::string& message);
    void info(const std::string& category, const std::string& message);
    void warning(const std::string& category, const std::string& message);
    void error(const std::string& category, const std::string& message);
    void fatal(const std::string& category, const std::string& message);

    // Get recent log entries (for crash report)
    std::vector<LogEntry> getRecentEntries(size_t count = 100) const;

    // Set the current operation context (shown in crash report)
    void setContext(const std::string& context);
    std::string getContext() const;

    // Enable/disable console output
    void setConsoleOutput(bool enabled) { m_consoleOutput = enabled; }

    // Enable/disable file output
    void setFileOutput(bool enabled, const std::string& path = "engine.log");

    // Set max entries to keep in memory
    void setMaxEntries(size_t max) { m_maxEntries = max; }

private:
    Logger();
    ~Logger();

    mutable std::mutex m_mutex;
    std::vector<LogEntry> m_entries;
    size_t m_maxEntries = 500;
    std::string m_currentContext;
    bool m_consoleOutput = true;
    bool m_fileOutput = false;
    std::string m_logFilePath;
};

// Crash Handler - catches crashes and generates reports
class CrashHandler {
public:
    static CrashHandler& instance();

    // Initialize the crash handler (call early in main())
    void initialize(const std::string& appName = "VoxelEngine",
                    const std::string& version = "1.0.0");

    // Shutdown (call before exit)
    void shutdown();

    // Set the crash log directory
    void setCrashLogDirectory(const std::string& path);

    // Set additional info to include in crash reports
    void setSystemInfo(const std::string& info);
    void setGPUInfo(const std::string& info);
    void setWorldInfo(const std::string& info);

    // Register a custom crash callback
    using CrashCallback = std::function<void(const std::string& crashLogPath)>;
    void setCallback(CrashCallback callback);

    // Manually trigger a crash report (for testing or soft crashes)
    void generateCrashReport(const std::string& reason);

    // Check if a crash log exists from a previous run
    bool hasPreviousCrashLog() const;
    std::string getPreviousCrashLogPath() const;

    // Get the crash log directory
    std::string getCrashLogDirectory() const { return m_crashLogDir; }

private:
    CrashHandler();
    ~CrashHandler();

    // Platform-specific crash handling
    void installHandlers();
    void uninstallHandlers();

    // Generate the crash report file
    std::string writeCrashReport(const std::string& reason, void* exceptionInfo = nullptr);

    // Get stack trace
    std::string captureStackTrace(void* context = nullptr);

    // Format system time
    std::string formatTimestamp(std::chrono::system_clock::time_point time);

    std::string m_appName;
    std::string m_version;
    std::string m_crashLogDir;
    std::string m_systemInfo;
    std::string m_gpuInfo;
    std::string m_worldInfo;
    CrashCallback m_callback;
    bool m_initialized = false;

    // For detecting if we're already handling a crash
    std::atomic<bool> m_handling{false};
};

// Scoped context - automatically sets/restores logger context
class ScopedContext {
public:
    ScopedContext(const std::string& context);
    ~ScopedContext();
private:
    std::string m_previousContext;
};

} // namespace Core

// Convenience macros
#define LOG_DEBUG(category, message) \
    Core::Logger::instance().log(Core::LogLevel::Debug, category, message, __FILE__, __LINE__)

#define LOG_INFO(category, message) \
    Core::Logger::instance().log(Core::LogLevel::Info, category, message, __FILE__, __LINE__)

#define LOG_WARNING(category, message) \
    Core::Logger::instance().log(Core::LogLevel::Warning, category, message, __FILE__, __LINE__)

#define LOG_ERROR(category, message) \
    Core::Logger::instance().log(Core::LogLevel::Error, category, message, __FILE__, __LINE__)

#define LOG_FATAL(category, message) \
    Core::Logger::instance().log(Core::LogLevel::Fatal, category, message, __FILE__, __LINE__)

#define LOG_CONTEXT(context) \
    Core::ScopedContext _scopedContext##__LINE__(context)
