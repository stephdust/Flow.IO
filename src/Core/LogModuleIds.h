#pragma once
/**
 * @file LogModuleIds.h
 * @brief Stable log module identifiers and helpers.
 */

#include <stdint.h>
#include <string.h>

using LogModuleId = uint16_t;

enum class LogModuleIdValue : LogModuleId {
    Unknown = 0,
    Core = 1,

    LogHub = 2,
    LogDispatcher = 3,
    LogSinkSerial = 4,
    LogSinkAlarm = 5,

    EventBusModule = 6,
    ConfigStoreModule = 7,
    DataStoreModule = 8,
    CommandModule = 9,
    AlarmModule = 10,
    WifiModule = 11,
    WifiProvisioningModule = 12,
    TimeModule = 13,
    I2cCfgClientModule = 14,
    I2cCfgServerModule = 15,
    WebInterfaceModule = 16,
    FirmwareUpdateModule = 17,
    SystemModule = 18,
    SystemMonitorModule = 19,
    HAModule = 20,
    MQTTModule = 21,
    IOModule = 22,
    PoolDeviceModule = 23,
    PoolLogicModule = 24,
    HMIModule = 25,

    CoreI2cLink = 40,
    CoreModuleManager = 41,
    CoreConfigStore = 42,
    CoreEventBus = 43
};

static inline LogModuleId logModuleIdFromModuleName(const char* moduleName)
{
    if (!moduleName || moduleName[0] == '\0') return (LogModuleId)LogModuleIdValue::Unknown;

    if (strcmp(moduleName, "loghub") == 0) return (LogModuleId)LogModuleIdValue::LogHub;
    if (strcmp(moduleName, "log.dispatcher") == 0) return (LogModuleId)LogModuleIdValue::LogDispatcher;
    if (strcmp(moduleName, "log.sink.serial") == 0) return (LogModuleId)LogModuleIdValue::LogSinkSerial;
    if (strcmp(moduleName, "log.sink.alarm") == 0) return (LogModuleId)LogModuleIdValue::LogSinkAlarm;

    if (strcmp(moduleName, "eventbus") == 0) return (LogModuleId)LogModuleIdValue::EventBusModule;
    if (strcmp(moduleName, "config") == 0) return (LogModuleId)LogModuleIdValue::ConfigStoreModule;
    if (strcmp(moduleName, "datastore") == 0) return (LogModuleId)LogModuleIdValue::DataStoreModule;
    if (strcmp(moduleName, "cmd") == 0) return (LogModuleId)LogModuleIdValue::CommandModule;
    if (strcmp(moduleName, "alarms") == 0) return (LogModuleId)LogModuleIdValue::AlarmModule;
    if (strcmp(moduleName, "wifi") == 0) return (LogModuleId)LogModuleIdValue::WifiModule;
    if (strcmp(moduleName, "wifiprov") == 0) return (LogModuleId)LogModuleIdValue::WifiProvisioningModule;
    if (strcmp(moduleName, "time") == 0) return (LogModuleId)LogModuleIdValue::TimeModule;
    if (strcmp(moduleName, "i2ccfg.client") == 0) return (LogModuleId)LogModuleIdValue::I2cCfgClientModule;
    if (strcmp(moduleName, "i2ccfg.server") == 0) return (LogModuleId)LogModuleIdValue::I2cCfgServerModule;
    if (strcmp(moduleName, "webinterface") == 0) return (LogModuleId)LogModuleIdValue::WebInterfaceModule;
    if (strcmp(moduleName, "fwupdate") == 0) return (LogModuleId)LogModuleIdValue::FirmwareUpdateModule;
    if (strcmp(moduleName, "system") == 0) return (LogModuleId)LogModuleIdValue::SystemModule;
    if (strcmp(moduleName, "sysmon") == 0) return (LogModuleId)LogModuleIdValue::SystemMonitorModule;
    if (strcmp(moduleName, "ha") == 0) return (LogModuleId)LogModuleIdValue::HAModule;
    if (strcmp(moduleName, "mqtt") == 0) return (LogModuleId)LogModuleIdValue::MQTTModule;
    if (strcmp(moduleName, "io") == 0) return (LogModuleId)LogModuleIdValue::IOModule;
    if (strcmp(moduleName, "pooldev") == 0) return (LogModuleId)LogModuleIdValue::PoolDeviceModule;
    if (strcmp(moduleName, "poollogic") == 0) return (LogModuleId)LogModuleIdValue::PoolLogicModule;
    if (strcmp(moduleName, "hmi") == 0) return (LogModuleId)LogModuleIdValue::HMIModule;

    return (LogModuleId)LogModuleIdValue::Unknown;
}

static inline const char* logModuleNameFromId(LogModuleId moduleId)
{
    switch ((LogModuleIdValue)moduleId) {
        case LogModuleIdValue::Core: return "core";
        case LogModuleIdValue::LogHub: return "loghub";
        case LogModuleIdValue::LogDispatcher: return "log.dispatcher";
        case LogModuleIdValue::LogSinkSerial: return "log.sink.serial";
        case LogModuleIdValue::LogSinkAlarm: return "log.sink.alarm";
        case LogModuleIdValue::EventBusModule: return "eventbus";
        case LogModuleIdValue::ConfigStoreModule: return "config";
        case LogModuleIdValue::DataStoreModule: return "datastore";
        case LogModuleIdValue::CommandModule: return "cmd";
        case LogModuleIdValue::AlarmModule: return "alarms";
        case LogModuleIdValue::WifiModule: return "wifi";
        case LogModuleIdValue::WifiProvisioningModule: return "wifiprov";
        case LogModuleIdValue::TimeModule: return "time";
        case LogModuleIdValue::I2cCfgClientModule: return "i2ccfg.client";
        case LogModuleIdValue::I2cCfgServerModule: return "i2ccfg.server";
        case LogModuleIdValue::WebInterfaceModule: return "webinterface";
        case LogModuleIdValue::FirmwareUpdateModule: return "fwupdate";
        case LogModuleIdValue::SystemModule: return "system";
        case LogModuleIdValue::SystemMonitorModule: return "sysmon";
        case LogModuleIdValue::HAModule: return "ha";
        case LogModuleIdValue::MQTTModule: return "mqtt";
        case LogModuleIdValue::IOModule: return "io";
        case LogModuleIdValue::PoolDeviceModule: return "pooldev";
        case LogModuleIdValue::PoolLogicModule: return "poollogic";
        case LogModuleIdValue::HMIModule: return "hmi";
        case LogModuleIdValue::CoreI2cLink: return "core.i2clink";
        case LogModuleIdValue::CoreModuleManager: return "core.modulemanager";
        case LogModuleIdValue::CoreConfigStore: return "core.configstore";
        case LogModuleIdValue::CoreEventBus: return "core.eventbus";
        case LogModuleIdValue::Unknown:
        default:
            return nullptr;
    }
}
