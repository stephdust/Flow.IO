#pragma once

#include "App/FirmwareProfile.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/Logs/LogAlarmSinkModule/LogAlarmSinkModule.h"
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"
#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/WebInterfaceModule/WebInterfaceModule.h"
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h"
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"

namespace Profiles {
namespace Supervisor {

struct ModuleInstances {
    ModuleInstances(const BoardSpec& board, const SupervisorRuntimeOptions& runtime);

    LogHubModule logHubModule{};
    LogDispatcherModule logDispatcherModule{};
    LogSerialSinkModule logSerialSinkModule{};
    EventBusModule eventBusModule{};
    ConfigStoreModule configStoreModule{};
    DataStoreModule dataStoreModule{};
    CommandModule commandModule{};
    AlarmModule alarmModule{};
    LogAlarmSinkModule logAlarmSinkModule{};
    WifiModule wifiModule{};
    WifiProvisioningModule wifiProvisioningModule{};
    TimeModule timeModule{};
    I2CCfgClientModule i2cCfgClientModule{};
    WebInterfaceModule webInterfaceModule;
    FirmwareUpdateModule firmwareUpdateModule;
    SystemModule systemModule{};
    SystemMonitorModule systemMonitorModule{};
    SupervisorHMIModule supervisorHMIModule;
};

ModuleInstances& moduleInstances();
const FirmwareProfile& profile();
const SupervisorRuntimeOptions& runtimeOptions();
void setupProfile(AppContext& ctx);
void loopProfile(AppContext& ctx);

}  // namespace Supervisor
}  // namespace Profiles
