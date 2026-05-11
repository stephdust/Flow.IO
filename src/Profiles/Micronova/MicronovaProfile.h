#pragma once

#include "App/FirmwareProfile.h"
#include "Core/SystemLimits.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IOModule.h"
#include "Modules/IOModule/IOModuleTypes.h"
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"
#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Micronova/MicronovaBusModule/MicronovaBusModule.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModule.h"
#include "Modules/Micronova/MicronovaMqttBridgeModule/MicronovaMqttBridgeModule.h"
#include "Modules/Network/HAModule/HAModule.h"
#include "Modules/Network/MQTTModule/MqttConfigModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/WebInterfaceModule/WebInterfaceModule.h"
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h"
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"

namespace Profiles {
namespace Micronova {

struct ModuleInstances {
    explicit ModuleInstances(const BoardSpec& board);

    LogHubModule logHubModule{};
    LogDispatcherModule logDispatcherModule{};
    LogSerialSinkModule logSerialSinkModule{};
    EventBusModule eventBusModule{};
    ConfigStoreModule configStoreModule{};
    DataStoreModule dataStoreModule{};
    CommandModule commandModule{};
    AlarmModule alarmModule{};
    WifiModule wifiModule;
    WifiProvisioningModule wifiProvisioningModule{};
    MqttConfigModule mqttConfigModule{};
    TimeModule timeModule{};
    MQTTModule mqttModule{};
    HAModule haModule{};
    IOModule ioModule;
    WebInterfaceModule webInterfaceModule;
    MicronovaBusModule micronovaBusModule;
    MicronovaBoilerModule micronovaBoilerModule{};
    MicronovaMqttBridgeModule micronovaMqttBridgeModule{};
    SystemModule systemModule{};
    SystemMonitorModule systemMonitorModule{};
    OneWireBus oneWireTemperature;
    IOBindingPortSpec ioBindingPorts[2]{};
    char topicNetworkState[Limits::TopicBuf] = {0};
    char topicSystemState[Limits::TopicBuf] = {0};
};

ModuleInstances& moduleInstances();
const FirmwareProfile& profile();
void setupProfile(AppContext& ctx);
void loopProfile(AppContext& ctx);

}  // namespace Micronova
}  // namespace Profiles
