/**
 * @file LogHubLModule.cpp
 * @brief Implementation file.
 */
#include "LogHubModule.h"
#include "Core/Log.h"
#include "Core/ConfigBranchIds.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/LogModuleIds.h"
#include "Core/SystemLimits.h"

void LogHubModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    hub.init(Limits::LogQueueLen);
    hub.attachConfig(&cfg, (uint8_t)ConfigModuleId::Log, (uint16_t)ConfigBranchId::LogLevels);

    /// expose loghub service
    hubSvc.enqueue = [](void* ctx, const LogEntry& e) -> bool {
        return static_cast<LogHub*>(ctx)->enqueue(e);
    };
    hubSvc.registerModule = [](void* ctx, LogModuleId moduleId, const char* moduleName) -> bool {
        return static_cast<LogHub*>(ctx)->registerModule(moduleId, moduleName);
    };
    hubSvc.shouldLog = [](void* ctx, LogModuleId moduleId, LogLevel level) -> bool {
        return static_cast<LogHub*>(ctx)->shouldLog(moduleId, level);
    };
    hubSvc.resolveModuleName = [](void* ctx, LogModuleId moduleId) -> const char* {
        return static_cast<const LogHub*>(ctx)->resolveModuleName(moduleId);
    };
    hubSvc.setModuleMinLevel = [](void* ctx, LogModuleId moduleId, LogLevel level) -> bool {
        return static_cast<LogHub*>(ctx)->setModuleMinLevel(moduleId, level);
    };
    hubSvc.getModuleMinLevel = [](void* ctx, LogModuleId moduleId) -> LogLevel {
        return static_cast<const LogHub*>(ctx)->getModuleMinLevel(moduleId);
    };
    hubSvc.ctx = &hub;

    /// expose sink registry service
    sinksSvc.add = [](void* ctx, LogSinkService sink) -> bool {
        return static_cast<LogSinkRegistry*>(ctx)->add(sink);
    };
    sinksSvc.count = [](void* ctx) -> int {
        return static_cast<LogSinkRegistry*>(ctx)->count();
    };
    sinksSvc.get = [](void* ctx, int idx) -> LogSinkService {
        return static_cast<LogSinkRegistry*>(ctx)->get(idx);
    };
    sinksSvc.ctx = &sinks;

    services.add("loghub", &hubSvc);
    services.add("logsinks", &sinksSvc);

    Log::setHub(&hubSvc);
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::LogHub, moduleId());
}
