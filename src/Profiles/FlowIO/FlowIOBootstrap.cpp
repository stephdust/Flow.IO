#include "Profiles/FlowIO/FlowIOProfile.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/ConfigMigrations.h"
#include "Core/DataStore/DataStore.h"
#include "Core/NvsKeys.h"
#include "Core/SnprintfCheck.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include "Core/WokwiDefaultOverrides.h"
#include "Core/Services/IFlowCfg.h"
#include "Domain/Pool/PoolBehaviors.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

namespace {

using Profiles::FlowIO::ModuleInstances;

const DomainIoBinding* findBindingByRole(const DomainSpec& domain, DomainRole role)
{
    for (uint8_t i = 0; i < domain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = domain.ioBindings[i];
        if (binding.role == role) return &binding;
    }
    return nullptr;
}

const DomainIoBinding* findBindingBySignal(const DomainSpec& domain, BoardSignal signal)
{
    for (uint8_t i = 0; i < domain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = domain.ioBindings[i];
        if (binding.signal == signal) return &binding;
    }
    return nullptr;
}

const PoolDevicePreset* findPoolPresetByRole(const DomainSpec& domain, DomainRole role)
{
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = domain.poolDevices[i];
        if (preset.role == role) return &preset;
    }
    return nullptr;
}

const DomainSensorPreset* findSensorPresetByRole(const DomainSpec& domain, DomainRole role)
{
    for (uint8_t i = 0; i < domain.sensorCount; ++i) {
        const DomainSensorPreset& preset = domain.sensors[i];
        if (preset.role == role) return &preset;
    }
    return nullptr;
}

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    while (true) delay(1000);
}

void onIoFloatValue(void* ctx, float value)
{
    ModuleInstances& modules = Profiles::FlowIO::moduleInstances();
    if (!modules.ioDataStore) return;
    const uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*modules.ioDataStore, idx, value, millis());
}

void onIoBoolValue(void* ctx, bool value)
{
    ModuleInstances& modules = Profiles::FlowIO::moduleInstances();
    if (!modules.ioDataStore) return;
    const uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointBool(*modules.ioDataStore, idx, value, millis());
}

void applyAnalogDefaultsForRole(DomainRole role, IOAnalogDefinition& def)
{
    switch (role) {
        case DomainRole::OrpSensor:
            def.source = FLOW_WIRDEF_IO_A0S;
            def.channel = FLOW_WIRDEF_IO_A0C;
            def.c0 = FLOW_WIRDEF_IO_A00;
            def.c1 = FLOW_WIRDEF_IO_A01;
            def.precision = FLOW_WIRDEF_IO_A0P;
            def.minValid = FLOW_WIRDEF_IO_A0N;
            def.maxValid = FLOW_WIRDEF_IO_A0X;
            break;
        case DomainRole::PhSensor:
            def.source = FLOW_WIRDEF_IO_A1S;
            def.channel = FLOW_WIRDEF_IO_A1C;
            def.c0 = FLOW_WIRDEF_IO_A10;
            def.c1 = FLOW_WIRDEF_IO_A11;
            def.precision = FLOW_WIRDEF_IO_A1P;
            def.minValid = FLOW_WIRDEF_IO_A1N;
            def.maxValid = FLOW_WIRDEF_IO_A1X;
            break;
        case DomainRole::PsiSensor:
            def.source = FLOW_WIRDEF_IO_A2S;
            def.channel = FLOW_WIRDEF_IO_A2C;
            def.c0 = FLOW_WIRDEF_IO_A20;
            def.c1 = FLOW_WIRDEF_IO_A21;
            def.precision = FLOW_WIRDEF_IO_A2P;
            def.minValid = FLOW_WIRDEF_IO_A2N;
            def.maxValid = FLOW_WIRDEF_IO_A2X;
            break;
        case DomainRole::SpareAnalog:
            def.source = FLOW_WIRDEF_IO_A3S;
            def.channel = FLOW_WIRDEF_IO_A3C;
            def.c0 = FLOW_WIRDEF_IO_A30;
            def.c1 = FLOW_WIRDEF_IO_A31;
            def.precision = FLOW_WIRDEF_IO_A3P;
            def.minValid = FLOW_WIRDEF_IO_A3N;
            def.maxValid = FLOW_WIRDEF_IO_A3X;
            break;
        case DomainRole::WaterTemp:
            def.source = FLOW_WIRDEF_IO_A4S;
            def.channel = FLOW_WIRDEF_IO_A4C;
            def.c0 = FLOW_WIRDEF_IO_A40;
            def.c1 = FLOW_WIRDEF_IO_A41;
            def.precision = FLOW_WIRDEF_IO_A4P;
            def.minValid = FLOW_WIRDEF_IO_A4N;
            def.maxValid = FLOW_WIRDEF_IO_A4X;
            break;
        case DomainRole::AirTemp:
            def.source = FLOW_WIRDEF_IO_A5S;
            def.channel = FLOW_WIRDEF_IO_A5C;
            def.c0 = FLOW_WIRDEF_IO_A50;
            def.c1 = FLOW_WIRDEF_IO_A51;
            def.precision = FLOW_WIRDEF_IO_A5P;
            def.minValid = FLOW_WIRDEF_IO_A5N;
            def.maxValid = FLOW_WIRDEF_IO_A5X;
            break;
        default:
            requireSetup(false, "unsupported analog domain role");
            break;
    }
}

bool buildNetworkSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = wifiIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    const bool netReady = wifiReady(*ds);
    const bool mqttOk = mqttReady(*ds);
    const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;

    const int wrote = snprintf(out, len,
                               "{\"ready\":%s,\"ip\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                               netReady ? "true" : "false",
                               ip,
                               rssi,
                               mqttOk ? "true" : "false",
                               (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    DataStore* ds = mqtt->dataStorePtr();
    const uint32_t rxDrop = ds ? mqttRxDrop(*ds) : 0U;
    const uint32_t parseFail = ds ? mqttParseFail(*ds) : 0U;
    const uint32_t handlerFail = ds ? mqttHandlerFail(*ds) : 0U;
    const uint32_t oversizeDrop = ds ? mqttOversizeDrop(*ds) : 0U;

    const int wrote = snprintf(
        out, len,
        "{\"upt_ms\":%llu,\"upt_s\":%llu,\"heap\":{\"free\":%lu,\"min_free\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"mqtt_rx\":{\"rx_drop\":%lu,\"oversize_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu},\"ts\":%lu}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long long)(snap.uptimeMs64 / 1000ULL),
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)rxDrop,
        (unsigned long)oversizeDrop,
        (unsigned long)parseFail,
        (unsigned long)handlerFail,
        (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

void startBootOrchestrator(ModuleInstances& modules)
{
    modules.bootOrchestrator.active = true;
    modules.bootOrchestrator.mqttReleased = false;
    modules.bootOrchestrator.haReleased = false;
    modules.bootOrchestrator.poolLogicReleased = false;
    modules.bootOrchestrator.t0Ms = millis();

    modules.mqttModule.setStartupReady(false);
    modules.haModule.setStartupReady(false);
    modules.poolLogicModule.setStartupReady(false);
}

void runBootOrchestrator(ModuleInstances& modules)
{
    if (!modules.bootOrchestrator.active) return;

    const uint32_t elapsed = millis() - modules.bootOrchestrator.t0Ms;

    if (!modules.bootOrchestrator.mqttReleased && elapsed >= Limits::Boot::MqttStartDelayMs) {
        modules.mqttModule.setStartupReady(true);
        modules.bootOrchestrator.mqttReleased = true;
    }

    if (!modules.bootOrchestrator.haReleased && elapsed >= Limits::Boot::HaStartDelayMs) {
        modules.haModule.setStartupReady(true);
        modules.bootOrchestrator.haReleased = true;
    }

    if (!modules.bootOrchestrator.poolLogicReleased && elapsed >= Limits::Boot::PoolLogicStartDelayMs) {
        modules.poolLogicModule.setStartupReady(true);
        modules.bootOrchestrator.poolLogicReleased = true;
    }

    if (modules.bootOrchestrator.mqttReleased &&
        modules.bootOrchestrator.haReleased &&
        modules.bootOrchestrator.poolLogicReleased) {
        modules.bootOrchestrator.active = false;
    }
}

void registerModules(AppContext& ctx, ModuleInstances& modules)
{
    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.i2cCfgServerModule);
    ctx.moduleManager.add(&modules.hmiModule);
    ctx.moduleManager.add(&modules.alarmModule);
    ctx.moduleManager.add(&modules.logAlarmSinkModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.timeModule);
    ctx.moduleManager.add(&modules.mqttModule);
    ctx.moduleManager.add(&modules.haModule);
    ctx.moduleManager.add(&modules.systemModule);
    ctx.moduleManager.add(&modules.ioModule);
    ctx.moduleManager.add(&modules.poolLogicModule);
    ctx.moduleManager.add(&modules.poolDeviceModule);

    modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
    ctx.moduleManager.add(&modules.systemMonitorModule);
}

void configureIoModule(const AppContext& ctx, ModuleInstances& modules)
{
    requireSetup(ctx.board != nullptr, "missing board spec");
    requireSetup(ctx.domain != nullptr, "missing domain spec");

    modules.ioModule.setOneWireBuses(&modules.oneWireWater, &modules.oneWireAir);

    for (uint8_t i = 0; i < ctx.domain->sensorCount; ++i) {
        const DomainSensorPreset& preset = ctx.domain->sensors[i];
        const DomainIoBinding* binding = findBindingByRole(*ctx.domain, preset.role);
        requireSetup(binding != nullptr, "missing domain sensor binding");

        const IoPointSpec* ioPoint = boardFindIoPoint(*ctx.board, binding->signal);
        requireSetup(ioPoint != nullptr, "missing board sensor point");

        const PoolSensorBinding* compat = PoolBinding::sensorBindingBySlot(preset.legacySlot);
        requireSetup(compat != nullptr, "missing compatibility sensor binding");

        if (preset.digitalInput) {
            IODigitalInputDefinition def{};
            snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
            def.ioId = compat->ioId;
            def.pin = ioPoint->pin;
            def.activeHigh = preset.activeHigh;
            def.pullMode = preset.pullMode;
            def.onValueChanged = onIoBoolValue;
            def.onValueCtx = (void*)(uintptr_t)compat->runtimeIndex;
            requireSetup(modules.ioModule.defineDigitalInput(def), "define digital input");
            continue;
        }

        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
        def.ioId = compat->ioId;
        def.onValueChanged = onIoFloatValue;
        def.onValueCtx = (void*)(uintptr_t)compat->runtimeIndex;
        applyAnalogDefaultsForRole(preset.role, def);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define analog input");
    }

    for (uint8_t i = 0; i < ctx.board->ioPointCount; ++i) {
        const IoPointSpec& point = ctx.board->ioPoints[i];
        if (point.capability != IoCapability::DigitalOut) continue;

        const DomainIoBinding* binding = findBindingBySignal(*ctx.domain, point.signal);
        requireSetup(binding != nullptr, "missing output domain binding");

        const PoolDevicePreset* preset = findPoolPresetByRole(*ctx.domain, binding->role);
        requireSetup(preset != nullptr, "missing output device preset");

        const PoolIoBinding* compat = PoolBinding::ioBindingBySlot(preset->legacySlot);
        requireSetup(compat != nullptr, "missing compatibility output binding");

        IODigitalOutputDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->objectSuffix ? compat->objectSuffix : "output");
        def.ioId = compat->ioId;
        def.pin = point.pin;
        def.activeHigh = false;
        def.initialOn = false;
        def.momentary = point.momentary;
        def.pulseMs = point.momentary ? point.pulseMs : 0;
        requireSetup(modules.ioModule.defineDigitalOutput(def), "define digital output");
    }
}

uint8_t dependsOnMaskForPreset(const DomainSpec& domain, const PoolDevicePreset& preset)
{
    if (preset.dependsOnRole == DomainRole::None) return 0;
    const PoolDevicePreset* dependency = findPoolPresetByRole(domain, preset.dependsOnRole);
    if (!dependency) return 0;
    return (uint8_t)(1u << dependency->legacySlot);
}

void configurePoolDevices(const AppContext& ctx, ModuleInstances& modules)
{
    for (uint8_t i = 0; i < ctx.domain->poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = ctx.domain->poolDevices[i];
        const PoolIoBinding* compat = PoolBinding::ioBindingBySlot(preset.legacySlot);
        requireSetup(compat != nullptr, "missing pool device compatibility binding");

        PoolDeviceDefinition def{};
        snprintf(def.label, sizeof(def.label), "%s", preset.displayName);
        def.ioId = compat->ioId;
        def.type = preset.poolDeviceType;
        def.flowLPerHour = preset.flowLPerHour;
        def.tankCapacityMl = preset.tankCapacityMl;
        def.tankInitialMl = preset.tankInitialMl;
        def.dependsOnMask = dependsOnMaskForPreset(*ctx.domain, preset);
        def.maxUptimeDaySec = preset.maxUptimeDaySec;
        requireSetup(modules.poolDeviceModule.defineDevice(def), "define pool device");
    }
}

void postInit(AppContext& ctx, ModuleInstances& modules)
{
    const DataStoreService* dsSvc = ctx.services.get<DataStoreService>("datastore");
    modules.ioDataStore = dsSvc ? dsSvc->store : nullptr;

    modules.mqttModule.formatTopic(modules.topicNetworkState, sizeof(modules.topicNetworkState), "rt/network/state");
    modules.mqttModule.formatTopic(modules.topicSystemState, sizeof(modules.topicSystemState), "rt/system/state");
    modules.mqttModule.addRuntimePublisher(modules.topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    modules.mqttModule.addRuntimePublisher(modules.topicSystemState, 60000, 0, false, buildSystemSnapshot);
    startBootOrchestrator(modules);
}

}  // namespace

namespace Profiles {
namespace FlowIO {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    ctx.registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);

    modules.mqttModule.setStartupReady(false);
    modules.haModule.setStartupReady(false);
    modules.poolLogicModule.setStartupReady(false);

    registerModules(ctx, modules);
    configureIoModule(ctx, modules);
    configurePoolDevices(ctx, modules);

    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.ioModule), "register runtime provider io");
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolDeviceModule), "register runtime provider pooldev");
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolLogicModule), "register runtime provider poollogic");

    requireSetup(ctx.moduleManager.initAll(ctx.registry, ctx.services), "init modules");
    postInit(ctx, modules);
}

void loopProfile(AppContext&)
{
    ModuleInstances& modules = moduleInstances();
    runBootOrchestrator(modules);
    delay(20);
}

}  // namespace FlowIO
}  // namespace Profiles
