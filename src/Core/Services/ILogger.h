#pragma once
/**
 * @file ILogger.h
 * @brief Logging service interfaces and helpers.
 */
#include "Core/LogModuleIds.h"
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>

/** @brief Log severity levels. */
enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

// ===== LOG TYPES =====
constexpr int LOG_MSG_MAX = 110;
constexpr int LOG_MODULE_NAME_MAX = 24;

/** @brief Fixed-size log entry. */
struct LogEntry {
    uint32_t ts_ms;
    LogLevel lvl;
    LogModuleId moduleId;
    char msg[LOG_MSG_MAX];
};

struct LogHubStatsSnapshot {
    uint16_t queueLen = 0;
    uint16_t queuedNow = 0;
    uint16_t peakQueued = 0;
    uint32_t enqueuedCount = 0;
    uint32_t droppedCount = 0;
    uint32_t formatTruncCount = 0;
    uint32_t lastDropMs = 0;
    uint32_t lastFormatTruncMs = 0;
    LogModuleId lastDropModuleId = (LogModuleId)LogModuleIdValue::Unknown;
    LogModuleId lastFormatTruncModuleId = (LogModuleId)LogModuleIdValue::Unknown;
};

/** @brief Log sink interface. */
struct LogSinkService {
    void (*write)(void* ctx, const LogEntry& e);
    void* ctx;
};

/** @brief Log hub interface (producer side). */
struct LogHubService {
    bool (*enqueue)(void* ctx, const LogEntry& e);
    bool (*registerModule)(void* ctx, LogModuleId moduleId, const char* moduleName);
    bool (*shouldLog)(void* ctx, LogModuleId moduleId, LogLevel level);
    const char* (*resolveModuleName)(void* ctx, LogModuleId moduleId);
    bool (*setModuleMinLevel)(void* ctx, LogModuleId moduleId, LogLevel level);
    LogLevel (*getModuleMinLevel)(void* ctx, LogModuleId moduleId);
    void (*getStats)(void* ctx, LogHubStatsSnapshot* out);
    void (*noteFormatTruncation)(void* ctx, LogModuleId moduleId, uint32_t wrote);
    void* ctx;
};

/** @brief Registry interface for log sinks. */
struct LogSinkRegistryService {
    bool (*add)(void* ctx, LogSinkService sink);
    int (*count)(void* ctx);
    LogSinkService (*get)(void* ctx, int index);
    void* ctx;
};

/** @brief Helper to format and enqueue a log entry. */
static inline void LOGHUBF(const LogHubService* s,
                           LogLevel lvl,
                           LogModuleId moduleId,
                           const char* fmt, ...) {
    if (!s || !s->enqueue) return;
    if (!fmt) return;
    if (s->shouldLog && !s->shouldLog(s->ctx, moduleId, lvl)) return;

    LogEntry e{};
    e.ts_ms = millis();
    e.lvl = lvl;
    e.moduleId = moduleId;

    va_list ap;
    va_start(ap, fmt);
    const int wrote = vsnprintf(e.msg, LOG_MSG_MAX, fmt, ap);
    va_end(ap);
    if ((wrote < 0 || wrote >= LOG_MSG_MAX) && s->noteFormatTruncation) {
        s->noteFormatTruncation(s->ctx, moduleId, (wrote < 0) ? 0U : (uint32_t)wrote);
    }

    // non bloquant (la queue peut drop si pleine)
    s->enqueue(s->ctx, e);
}
