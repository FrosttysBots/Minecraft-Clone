#include "CrashHandler.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <signal.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace Core {

// ============================================================================
// Logger Implementation
// ============================================================================

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    m_entries.reserve(m_maxEntries);
}

Logger::~Logger() = default;

void Logger::log(LogLevel level, const std::string& category, const std::string& message,
                 const char* file, int line) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.file = file ? file : "";
    entry.line = line;

    // Thread-safe entry addition
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_entries.size() >= m_maxEntries) {
            // Remove oldest entries (keep last 75%)
            size_t removeCount = m_maxEntries / 4;
            m_entries.erase(m_entries.begin(), m_entries.begin() + removeCount);
        }
        m_entries.push_back(entry);
    }

    // Console output
    if (m_consoleOutput) {
        const char* levelStr = "";
        const char* colorCode = "";
        switch (level) {
            case LogLevel::Debug:   levelStr = "DEBUG"; colorCode = "\033[90m"; break;
            case LogLevel::Info:    levelStr = "INFO";  colorCode = "\033[37m"; break;
            case LogLevel::Warning: levelStr = "WARN";  colorCode = "\033[33m"; break;
            case LogLevel::Error:   levelStr = "ERROR"; colorCode = "\033[31m"; break;
            case LogLevel::Fatal:   levelStr = "FATAL"; colorCode = "\033[91m"; break;
        }

        auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
        std::tm tm;
        localtime_s(&tm, &time);

        std::cout << colorCode << "["
                  << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
                  << std::setfill('0') << std::setw(2) << tm.tm_min << ":"
                  << std::setfill('0') << std::setw(2) << tm.tm_sec << "] "
                  << "[" << levelStr << "] "
                  << "[" << category << "] "
                  << message
                  << "\033[0m" << std::endl;
    }

    // File output
    if (m_fileOutput && !m_logFilePath.empty()) {
        std::ofstream file(m_logFilePath, std::ios::app);
        if (file.is_open()) {
            auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
            std::tm tm;
            localtime_s(&tm, &time);

            const char* levelStr = "";
            switch (level) {
                case LogLevel::Debug:   levelStr = "DEBUG"; break;
                case LogLevel::Info:    levelStr = "INFO";  break;
                case LogLevel::Warning: levelStr = "WARN";  break;
                case LogLevel::Error:   levelStr = "ERROR"; break;
                case LogLevel::Fatal:   levelStr = "FATAL"; break;
            }

            file << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                 << "[" << levelStr << "] "
                 << "[" << category << "] "
                 << message;

            if (!entry.file.empty()) {
                // Extract just the filename
                std::filesystem::path p(entry.file);
                file << " (" << p.filename().string() << ":" << entry.line << ")";
            }

            file << std::endl;
        }
    }
}

void Logger::debug(const std::string& category, const std::string& message) {
    log(LogLevel::Debug, category, message);
}

void Logger::info(const std::string& category, const std::string& message) {
    log(LogLevel::Info, category, message);
}

void Logger::warning(const std::string& category, const std::string& message) {
    log(LogLevel::Warning, category, message);
}

void Logger::error(const std::string& category, const std::string& message) {
    log(LogLevel::Error, category, message);
}

void Logger::fatal(const std::string& category, const std::string& message) {
    log(LogLevel::Fatal, category, message);
}

std::vector<LogEntry> Logger::getRecentEntries(size_t count) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (count >= m_entries.size()) {
        return m_entries;
    }
    return std::vector<LogEntry>(m_entries.end() - count, m_entries.end());
}

void Logger::setContext(const std::string& context) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentContext = context;
}

std::string Logger::getContext() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentContext;
}

void Logger::setFileOutput(bool enabled, const std::string& path) {
    m_fileOutput = enabled;
    m_logFilePath = path;
    if (enabled && !path.empty()) {
        // Create/clear the log file
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &time);
            file << "=== VoxelEngine Log Started: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " ===" << std::endl;
        }
    }
}

// ============================================================================
// CrashHandler Implementation
// ============================================================================

#ifdef _WIN32
// Global pointer for the exception filter
static CrashHandler* g_crashHandler = nullptr;

// Exception filter callback
static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    if (g_crashHandler) {
        g_crashHandler->generateCrashReport("Unhandled Exception");
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Signal handlers
static void signalHandler(int signal) {
    if (g_crashHandler) {
        std::string reason;
        switch (signal) {
            case SIGABRT: reason = "Abort signal (SIGABRT)"; break;
            case SIGFPE:  reason = "Floating point exception (SIGFPE)"; break;
            case SIGILL:  reason = "Illegal instruction (SIGILL)"; break;
            case SIGSEGV: reason = "Segmentation fault (SIGSEGV)"; break;
            default:      reason = "Signal " + std::to_string(signal); break;
        }
        g_crashHandler->generateCrashReport(reason);
    }
    std::exit(1);
}
#endif

CrashHandler& CrashHandler::instance() {
    static CrashHandler handler;
    return handler;
}

CrashHandler::CrashHandler() {
    // Default crash log directory
    m_crashLogDir = "crash_logs";
}

CrashHandler::~CrashHandler() {
    if (m_initialized) {
        shutdown();
    }
}

void CrashHandler::initialize(const std::string& appName, const std::string& version) {
    m_appName = appName;
    m_version = version;
    m_initialized = true;

    // Create crash log directory
    std::filesystem::create_directories(m_crashLogDir);

    // Install platform-specific handlers
    installHandlers();

    LOG_INFO("CrashHandler", "Crash handler initialized - logs will be saved to: " + m_crashLogDir);
}

void CrashHandler::shutdown() {
    uninstallHandlers();
    m_initialized = false;
}

void CrashHandler::setCrashLogDirectory(const std::string& path) {
    m_crashLogDir = path;
    std::filesystem::create_directories(m_crashLogDir);
}

void CrashHandler::setSystemInfo(const std::string& info) {
    m_systemInfo = info;
}

void CrashHandler::setGPUInfo(const std::string& info) {
    m_gpuInfo = info;
}

void CrashHandler::setWorldInfo(const std::string& info) {
    m_worldInfo = info;
}

void CrashHandler::setCallback(CrashCallback callback) {
    m_callback = callback;
}

void CrashHandler::installHandlers() {
#ifdef _WIN32
    g_crashHandler = this;

    // Set the unhandled exception filter
    SetUnhandledExceptionFilter(unhandledExceptionFilter);

    // Install signal handlers
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
    signal(SIGSEGV, signalHandler);

    // Initialize symbol handler for stack traces
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#endif
}

void CrashHandler::uninstallHandlers() {
#ifdef _WIN32
    g_crashHandler = nullptr;
    SetUnhandledExceptionFilter(NULL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    SymCleanup(GetCurrentProcess());
#endif
}

std::string CrashHandler::captureStackTrace(void* context) {
    std::stringstream ss;

#ifdef _WIN32
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    CONTEXT ctx;
    if (context) {
        ctx = *static_cast<CONTEXT*>(context);
    } else {
        RtlCaptureContext(&ctx);
    }

    STACKFRAME64 frame = {};
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

#ifdef _M_X64
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#else
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif

    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int frameNum = 0;
    while (StackWalk64(machineType, process, thread, &frame, &ctx,
                       NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        if (frame.AddrPC.Offset == 0) break;
        if (frameNum >= 50) break;  // Limit stack depth

        ss << "  [" << std::setw(2) << frameNum << "] ";

        DWORD64 displacement64 = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement64, symbol)) {
            ss << symbol->Name;

            DWORD displacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &displacement, &line)) {
                std::filesystem::path p(line.FileName);
                ss << " (" << p.filename().string() << ":" << line.LineNumber << ")";
            }
        } else {
            ss << "0x" << std::hex << frame.AddrPC.Offset << std::dec;
        }

        ss << "\n";
        frameNum++;
    }

    if (frameNum == 0) {
        ss << "  (Unable to capture stack trace)\n";
    }
#else
    ss << "  (Stack trace not available on this platform)\n";
#endif

    return ss.str();
}

std::string CrashHandler::formatTimestamp(std::chrono::system_clock::time_point time) {
    auto t = std::chrono::system_clock::to_time_t(time);
    std::tm tm;
    localtime_s(&tm, &t);

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return ss.str();
}

void CrashHandler::generateCrashReport(const std::string& reason) {
    // Prevent recursive crash handling
    bool expected = false;
    if (!m_handling.compare_exchange_strong(expected, true)) {
        return;
    }

    writeCrashReport(reason, nullptr);
}

std::string CrashHandler::writeCrashReport(const std::string& reason, void* exceptionInfo) {
    auto now = std::chrono::system_clock::now();
    std::string timestamp = formatTimestamp(now);
    std::string filename = m_crashLogDir + "/crash_" + timestamp + ".log";

    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "FATAL: Could not create crash log file: " << filename << std::endl;
        return "";
    }

    // Header
    file << "================================================================================\n";
    file << "                         VOXEL ENGINE CRASH REPORT\n";
    file << "================================================================================\n\n";

    // Crash info
    file << "Application:    " << m_appName << "\n";
    file << "Version:        " << m_version << "\n";

    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    file << "Crash Time:     " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
    file << "Crash Reason:   " << reason << "\n";

    // Current context
    std::string context = Logger::instance().getContext();
    if (!context.empty()) {
        file << "Current Task:   " << context << "\n";
    }

    file << "\n";

    // System info
    file << "--------------------------------------------------------------------------------\n";
    file << "SYSTEM INFORMATION\n";
    file << "--------------------------------------------------------------------------------\n";

    if (!m_systemInfo.empty()) {
        file << m_systemInfo << "\n";
    } else {
#ifdef _WIN32
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        file << "Processors:     " << sysInfo.dwNumberOfProcessors << "\n";

        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            file << "Total Memory:   " << (memStatus.ullTotalPhys / (1024 * 1024)) << " MB\n";
            file << "Available Mem:  " << (memStatus.ullAvailPhys / (1024 * 1024)) << " MB\n";
            file << "Memory Load:    " << memStatus.dwMemoryLoad << "%\n";
        }

        OSVERSIONINFOEX osInfo;
        osInfo.dwOSVersionInfoSize = sizeof(osInfo);
        file << "Platform:       Windows\n";
#endif
    }

    // GPU info
    if (!m_gpuInfo.empty()) {
        file << "\nGPU Information:\n" << m_gpuInfo << "\n";
    }

    file << "\n";

    // World info
    if (!m_worldInfo.empty()) {
        file << "--------------------------------------------------------------------------------\n";
        file << "WORLD STATE\n";
        file << "--------------------------------------------------------------------------------\n";
        file << m_worldInfo << "\n\n";
    }

    // Stack trace
    file << "--------------------------------------------------------------------------------\n";
    file << "STACK TRACE\n";
    file << "--------------------------------------------------------------------------------\n";
#ifdef _WIN32
    if (exceptionInfo) {
        EXCEPTION_POINTERS* ep = static_cast<EXCEPTION_POINTERS*>(exceptionInfo);
        file << captureStackTrace(ep->ContextRecord);
    } else {
        file << captureStackTrace(nullptr);
    }
#else
    file << "(Stack trace not available)\n";
#endif

    file << "\n";

    // Recent log entries
    file << "--------------------------------------------------------------------------------\n";
    file << "RECENT LOG ENTRIES (Last 100)\n";
    file << "--------------------------------------------------------------------------------\n";

    auto entries = Logger::instance().getRecentEntries(100);
    for (const auto& entry : entries) {
        auto entryTime = std::chrono::system_clock::to_time_t(entry.timestamp);
        std::tm entryTm;
        localtime_s(&entryTm, &entryTime);

        const char* levelStr = "";
        switch (entry.level) {
            case LogLevel::Debug:   levelStr = "DEBUG"; break;
            case LogLevel::Info:    levelStr = "INFO "; break;
            case LogLevel::Warning: levelStr = "WARN "; break;
            case LogLevel::Error:   levelStr = "ERROR"; break;
            case LogLevel::Fatal:   levelStr = "FATAL"; break;
        }

        file << "[" << std::put_time(&entryTm, "%H:%M:%S") << "] "
             << "[" << levelStr << "] "
             << "[" << entry.category << "] "
             << entry.message;

        if (!entry.file.empty()) {
            std::filesystem::path p(entry.file);
            file << " (" << p.filename().string() << ":" << entry.line << ")";
        }

        file << "\n";
    }

    file << "\n";
    file << "================================================================================\n";
    file << "                              END OF CRASH REPORT\n";
    file << "================================================================================\n";

    file.close();

    // Also output to console
    std::cerr << "\n\n========================================\n";
    std::cerr << "CRASH DETECTED: " << reason << "\n";
    std::cerr << "Crash log saved to: " << filename << "\n";
    std::cerr << "========================================\n\n";

    // Call the callback if set
    if (m_callback) {
        m_callback(filename);
    }

    return filename;
}

bool CrashHandler::hasPreviousCrashLog() const {
    if (!std::filesystem::exists(m_crashLogDir)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_crashLogDir)) {
        if (entry.path().extension() == ".log") {
            return true;
        }
    }
    return false;
}

std::string CrashHandler::getPreviousCrashLogPath() const {
    if (!std::filesystem::exists(m_crashLogDir)) {
        return "";
    }

    std::filesystem::path newestLog;
    std::filesystem::file_time_type newestTime;

    for (const auto& entry : std::filesystem::directory_iterator(m_crashLogDir)) {
        if (entry.path().extension() == ".log") {
            auto writeTime = entry.last_write_time();
            if (newestLog.empty() || writeTime > newestTime) {
                newestLog = entry.path();
                newestTime = writeTime;
            }
        }
    }

    return newestLog.string();
}

// ============================================================================
// ScopedContext Implementation
// ============================================================================

ScopedContext::ScopedContext(const std::string& context) {
    m_previousContext = Logger::instance().getContext();
    Logger::instance().setContext(context);
}

ScopedContext::~ScopedContext() {
    Logger::instance().setContext(m_previousContext);
}

} // namespace Core
