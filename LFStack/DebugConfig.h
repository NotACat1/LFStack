#pragma once

// 1. Platform-Specific Debugging (Memory Leaks)
// _CRTDBG macros are specific to MSVC. We wrap them in _MSC_VER to prevent
// compilation errors if this header is ever compiled on GCC or Clang.
#if defined(_DEBUG) && defined(_MSC_VER)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

// 2. Standard Library Includes
#include <iostream>
#include <source_location>
#include <string_view>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <string>

#ifdef _DEBUG

namespace DebugUtils {

    /**
     * @brief Global mutex for synchronizing console output across threads.
     * Uses a Meyer's Singleton for safe initialization.
     */
    inline std::mutex& GetLogMutex() {
        static std::mutex mtx;
        return mtx;
    }

    /**
     * @brief Prints the current system time in [HH:MM:SS] format.
     * Note: Not locked internally. Must be called within a locked context.
     */
    inline void PrintTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm bt{};

#if defined(_MSC_VER)
        localtime_s(&bt, &in_time_t);     // Secure MSVC version
#else
        localtime_r(&in_time_t, &bt);     // POSIX safe version
#endif
        std::cout << std::put_time(&bt, "[%H:%M:%S] ");
    }

    /**
     * @brief Core logging function. Formats and prints thread-safe messages.
     */
    inline void LogInternal(std::string_view level, std::string_view message, const std::source_location loc) {
        std::lock_guard<std::mutex> lock(GetLogMutex());

        PrintTimestamp();

        // Strip directory path to keep the log clean (just filename)
        std::string_view file = loc.file_name();
        if (const auto pos = file.find_last_of("\\/"); pos != std::string_view::npos) {
            file.remove_prefix(pos + 1);
        }

        std::cout << "[" << level << "] "
            << file << ":" << loc.line() << " in " << loc.function_name()
            << "\n  > " << message << "\n" << std::endl; // std::endl flushes the stream
    }

    /**
     * @brief RAII-based execution timer. Measures time spent in the current scope.
     */
    class ScopedTimer {
    public:
        explicit ScopedTimer(std::string_view name, const std::source_location loc = std::source_location::current())
            : m_name(name), m_loc(loc), m_start(std::chrono::high_resolution_clock::now()) {
        }

        ~ScopedTimer() {
            const auto end = std::chrono::high_resolution_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - m_start).count();

            std::lock_guard<std::mutex> lock(GetLogMutex());
            std::cout << "[TIMER] " << m_loc.function_name() << " | '" << m_name
                << "' took " << ms << " ms\n" << std::endl;
        }

    private:
        std::string_view m_name;
        std::source_location m_loc;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
    };
} // namespace DebugUtils

// 3. Macro Utilities for safe variable concatenation
#define DEBUG_CONCAT_IMPL(x, y) x##y
#define DEBUG_CONCAT(x, y) DEBUG_CONCAT_IMPL(x, y)

// 4. Public API Macros (Debug Mode)
#define DEBUG_LOG(msg) DebugUtils::LogInternal("DEBUG", msg, std::source_location::current())
#define DEBUG_ERR(msg) DebugUtils::LogInternal("ERROR", msg, std::source_location::current())

// Uses DEBUG_CONCAT to ensure multiple timers in the same function don't cause variable name collisions
#define DEBUG_TIMER(name) DebugUtils::ScopedTimer DEBUG_CONCAT(timer_, __LINE__)(name, std::source_location::current())

class DebugManager {
public:
    static void Initialize() {
#if defined(_MSC_VER)
        // Enable MSVC memory leak reporting on application exit
        int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
        flags |= _CRTDBG_ALLOC_MEM_DF;
        flags |= _CRTDBG_LEAK_CHECK_DF;
        _CrtSetDbgFlag(flags);

        // Direct CRT reports to standard output for better visibility
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

        std::lock_guard<std::mutex> lock(DebugUtils::GetLogMutex());
        std::cout << "==================================================\n"
            << "[SYSTEM] Debug Manager Initialized. Thread Safe: ON\n"
            << "==================================================\n" << std::endl;
    }
};

namespace {
    // Triggers DebugManager::Initialize() automatically before main()
    struct AutoInit { AutoInit() { DebugManager::Initialize(); } };
    static AutoInit _unused_init;
}

// 5. Memory Tracking 'new' Redefinition
// CRITICAL: This MUST be at the very bottom of the header file.
// Redefining 'new' before standard library includes can corrupt STL headers.
#if defined(_MSC_VER) && !defined(DBG_NEW)
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif

#else

// 6. Public API Macros (Release Mode / Disabled)
// Uses `do {} while (0)` to swallow the statement safely without triggering compiler warnings.
#define DEBUG_LOG(msg) do {} while (0)
#define DEBUG_ERR(msg) do {} while (0)
#define DEBUG_TIMER(name) do {} while (0)

#endif // _DEBUG