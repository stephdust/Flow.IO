/**
 * @file WebInterfaceLog.cpp
 * @brief Log formatting helpers and local sink fan-out for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#include <time.h>

bool WebInterfaceModule::isLogByte_(uint8_t c)
{
    return c == '\t' || c == 0x1B || c >= 32;
}

char WebInterfaceModule::levelChar_(LogLevel lvl)
{
    if (lvl == LogLevel::Debug) return 'D';
    if (lvl == LogLevel::Info) return 'I';
    if (lvl == LogLevel::Warn) return 'W';
    if (lvl == LogLevel::Error) return 'E';
    return '?';
}

const char* WebInterfaceModule::levelColor_(LogLevel lvl)
{
    if (lvl == LogLevel::Debug) return "\x1b[90m";
    if (lvl == LogLevel::Info) return "\x1b[32m";
    if (lvl == LogLevel::Warn) return "\x1b[33m";
    if (lvl == LogLevel::Error) return "\x1b[31m";
    return "";
}

const char* WebInterfaceModule::colorReset_()
{
    return "\x1b[0m";
}

bool WebInterfaceModule::isSystemTimeValid_()
{
    time_t now = time(nullptr);
    return (now > 1609459200); // 2021-01-01 00:00:00
}

void WebInterfaceModule::formatUptime_(char* out, size_t outSize, uint32_t ms)
{
    uint32_t s = ms / 1000;
    uint32_t m = s / 60;
    uint32_t h = m / 60;
    const uint32_t hh = h % 24;
    const uint32_t mm = m % 60;
    const uint32_t ss = s % 60;
    const uint32_t mmm = ms % 1000;

    snprintf(out, outSize, "%02lu:%02lu:%02lu.%03lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss,
             (unsigned long)mmm);
}

void WebInterfaceModule::formatTimestamp_(WebInterfaceModule* self, const LogEntry& e, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';

    bool timeFromService = false;
    if (self) {
        if (!self->timeSvc_ && self->services_) {
            self->timeSvc_ = self->services_->get<TimeService>(ServiceId::Time);
        }
        if (self->timeSvc_ &&
            self->timeSvc_->isSynced &&
            self->timeSvc_->formatLocalTime &&
            self->timeSvc_->isSynced(self->timeSvc_->ctx)) {
            char localTs[32] = {0};
            if (self->timeSvc_->formatLocalTime(self->timeSvc_->ctx, localTs, sizeof(localTs))) {
                const unsigned ms = e.ts_ms % 1000;
                snprintf(out, outSize, "%s.%03u", localTs, ms);
                timeFromService = true;
            }
        }
    }

    if (!timeFromService && isSystemTimeValid_()) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        const unsigned ms = e.ts_ms % 1000;
        snprintf(out, outSize,
                 "%04d-%02d-%02d %02d:%02d:%02d.%03u",
                 t.tm_year + 1900,
                 t.tm_mon + 1,
                 t.tm_mday,
                 t.tm_hour,
                 t.tm_min,
                 t.tm_sec,
                 ms);
        return;
    }

    if (!timeFromService) {
        formatUptime_(out, outSize, e.ts_ms);
    }
}

void WebInterfaceModule::onLocalLogSinkWrite_(void* ctx, const LogEntry& e)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self || !self->localLogQueue_) return;
    const char* moduleName = nullptr;
    char moduleFallback[24] = {0};
    if (self->logHub_ && self->logHub_->resolveModuleName) {
        moduleName = self->logHub_->resolveModuleName(self->logHub_->ctx, e.moduleId);
    }
    if (!moduleName || moduleName[0] == '\0') {
        snprintf(moduleFallback, sizeof(moduleFallback), "#%u", (unsigned)e.moduleId);
        moduleName = moduleFallback;
    }
    const char* msg = e.msg;
    const char* color = levelColor_(e.lvl);
    char ts[48] = {0};
    formatTimestamp_(self, e, ts, sizeof(ts));

    char line[kLocalLogLineMax] = {0};
    int wrote = snprintf(line,
                         sizeof(line),
                         "[%s][%c][%s] %s%s",
                         ts,
                         levelChar_(e.lvl),
                         moduleName,
                         color,
                         msg);
    if (wrote > 0 && (size_t)wrote < sizeof(line)) {
        wrote += snprintf(line + wrote, sizeof(line) - (size_t)wrote, "%s", colorReset_());
    }
    if (wrote <= 0) return;
    if ((size_t)wrote >= sizeof(line)) {
        line[sizeof(line) - 1] = '\0';
    }

    if (xQueueSend(self->localLogQueue_, line, 0) != pdTRUE) {
        char dropped[kLocalLogLineMax] = {0};
        (void)xQueueReceive(self->localLogQueue_, dropped, 0);
        (void)xQueueSend(self->localLogQueue_, line, 0);
    }
}

void WebInterfaceModule::flushLocalLogQueue_()
{
    if (!started_ || !localLogQueue_) return;

    char line[kLocalLogLineMax] = {0};
    while (xQueueReceive(localLogQueue_, line, 0) == pdTRUE) {
        wsLog_.textAll(line);
    }
}
