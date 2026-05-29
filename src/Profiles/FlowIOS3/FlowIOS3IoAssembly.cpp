#include "Profiles/FlowIOS3/FlowIOS3IoAssembly.h"
#include "Profiles/FlowIOS3/FlowIOS3IoLayout.h"

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/MqttTopics.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include "Profiles/FlowIOS3/FlowIOS3Profile.h"

#ifndef FLOW_HA_BOOT_TRACE
#define FLOW_HA_BOOT_TRACE 0
#endif

#if FLOW_HA_BOOT_TRACE
#define FLOWIOS3_HA_BOOT_TRACE(FMT, ...) Board::SerialMap::logSerial().printf("[HA-BOOT] " FMT "\r\n", ##__VA_ARGS__)
#else
#define FLOWIOS3_HA_BOOT_TRACE(FMT, ...) do {} while (0)
#endif

namespace {

using Profiles::FlowIOS3::ModuleInstances;
namespace FlowIoLayout = Profiles::FlowIOS3::IoLayout;
static constexpr uint8_t kFlowIoAnalogHaSlots = 17;

struct FlowIoAnalogHaSpec {
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
    const char* unit = nullptr;
};

struct FlowIoDigitalHaSpec {
    uint8_t logicalIdx = 0;
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
    const char* unit = nullptr;
};

constexpr FlowIoAnalogHaSpec kAnalogHaSpecs[kFlowIoAnalogHaSlots] = {
    {"io_orp", "ORP", "mdi:flash", "mV"},
    {"io_ph", "pH", "mdi:ph", ""},
    {"io_psi", "PSI", "mdi:gauge", "PSI"},
    {"io_spare", "Spare", "mdi:sine-wave", nullptr},
    {"io_wat_tmp", "Water Temperature", "mdi:water-thermometer", "\xC2\xB0""C"},
    {"io_air_tmp", "Air Temperature", "mdi:thermometer", "\xC2\xB0""C"},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
};

constexpr FlowIoDigitalHaSpec kDigitalHaSpecs[] = {
    {0, "io_pool_lvl", "Pool Level", "mdi:waves-arrow-up", nullptr},
    {1, "io_ph_lvl", "pH Level", "mdi:flask-outline", nullptr},
    {2, "io_chl_lvl", "Chlorine Level", "mdi:test-tube", nullptr},
    {3, "io_wat_cnt", "Water Counter", "mdi:water-sync", "L"},
};

struct FlowIoDiscoveryHeap {
    char analogObjectSuffix[kFlowIoAnalogHaSlots][24]{};
    char analogFallbackName[kFlowIoAnalogHaSlots][24]{};
    char analogValueTpl[kFlowIoAnalogHaSlots][128]{};
    char analogStateSuffix[kFlowIoAnalogHaSlots][24]{};
    char digitalStateSuffix[sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])][24]{};
    char switchStateSuffix[PoolBinding::kDeviceBindingCount][24]{};
    char switchPayloadOn[PoolBinding::kDeviceBindingCount][Limits::IoHaSwitchPayloadBuf]{};
    char switchPayloadOff[PoolBinding::kDeviceBindingCount][Limits::IoHaSwitchPayloadBuf]{};
};

FlowIoDiscoveryHeap* gDiscoveryHeap = nullptr;
bool gDiscoveryHeapReleaseWaitLogged = false;
bool gOneShotRefreshBypassedLogged = false;

bool ensureDiscoveryHeap()
{
    if (gDiscoveryHeap) return true;
    gDiscoveryHeap = static_cast<FlowIoDiscoveryHeap*>(
        heap_caps_calloc(1, sizeof(FlowIoDiscoveryHeap), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    if (gDiscoveryHeap) {
        FLOWIOS3_HA_BOOT_TRACE("FlowIOS3 discovery heap allocated (%u bytes)", (unsigned)sizeof(FlowIoDiscoveryHeap));
    } else {
        FLOWIOS3_HA_BOOT_TRACE("FlowIOS3 discovery heap allocation failed (%u bytes)", (unsigned)sizeof(FlowIoDiscoveryHeap));
    }
    return gDiscoveryHeap != nullptr;
}

void releaseDiscoveryHeapIfReady(ModuleInstances& modules)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gDiscoveryHeap || !modules.ioDataStore) return;
    if (!haAutoconfigPublished(*modules.ioDataStore)) {
        if (!gDiscoveryHeapReleaseWaitLogged) {
            FLOWIOS3_HA_BOOT_TRACE("FlowIOS3 discovery heap waiting for HA publish completion");
            gDiscoveryHeapReleaseWaitLogged = true;
        }
        return;
    }
    heap_caps_free(gDiscoveryHeap);
    gDiscoveryHeap = nullptr;
    gDiscoveryHeapReleaseWaitLogged = false;
    FLOWIOS3_HA_BOOT_TRACE("FlowIOS3 discovery heap released after HA one-shot publish");
#else
    (void)modules;
#endif
}

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

uint8_t digitalInputOrdinalFromPort(PhysicalPortId port)
{
    switch (port) {
        case FlowIoLayout::PortDigitalIn1: return 1;
        case FlowIoLayout::PortDigitalIn2: return 2;
        case FlowIoLayout::PortDigitalIn3: return 3;
        case FlowIoLayout::PortDigitalIn4: return 4;
        case FlowIoLayout::PortDigitalIn5: return 5;
        case FlowIoLayout::PortDigitalIn6: return 6;
        case FlowIoLayout::PortDigitalIn7: return 7;
        case FlowIoLayout::PortDigitalIn8: return 8;
        default: return 0;
    }
}

uint8_t exioOrdinalFromPort(PhysicalPortId port)
{
    switch (port) {
        case FlowIoLayout::PortExio1: return 1;
        case FlowIoLayout::PortExio2: return 2;
        case FlowIoLayout::PortExio3: return 3;
        case FlowIoLayout::PortExio4: return 4;
        case FlowIoLayout::PortExio5: return 5;
        case FlowIoLayout::PortExio6: return 6;
        case FlowIoLayout::PortExio7: return 7;
        case FlowIoLayout::PortExio8: return 8;
        default: return 0;
    }
}

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    while (true) delay(1000);
}

void applyAnalogDefaultsForRole(DomainRole role, IOAnalogDefinition& def)
{
    const FlowIoLayout::AnalogRoleDefault* spec = FlowIoLayout::analogDefaultForRole(role);
    requireSetup(spec != nullptr, "unsupported analog domain role");
    def.bindingPort = spec->bindingPort;
    def.c0 = spec->c0;
    def.c1 = spec->c1;
    def.precision = spec->precision;
}

void applyDigitalDefaultsForRole(DomainRole role, IODigitalInputDefinition& def)
{
    const FlowIoLayout::DigitalInputRoleDefault* spec = FlowIoLayout::digitalInputDefaultForRole(role);
    requireSetup(spec != nullptr, "unsupported digital input domain role");
    def.bindingPort = spec->bindingPort;
    def.mode = spec->mode;
    def.edgeMode = spec->edgeMode;
    def.counterDebounceUs = spec->debounceUs;
}

void buildAnalogValueTemplate(const IOModule& ioModule, uint8_t analogIdx, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    const int32_t precision = ioModule.analogPrecision(analogIdx);
    snprintf(
        out,
        outLen,
        "{%% if value_json.value is number %%}{{ value_json.value | float | round(%ld) }}{%% else %%}unavailable{%% endif %%}",
        (long)precision
    );
}

void syncAnalogSensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSensor) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    for (uint8_t i = 0; i < kFlowIoAnalogHaSlots; ++i) {
        if (!modules.ioModule.analogSlotPublished(i)) continue;
        const FlowIoAnalogHaSpec& spec = kAnalogHaSpecs[i];

        buildAnalogValueTemplate(
            modules.ioModule,
            i,
            gDiscoveryHeap->analogValueTpl[i],
            sizeof(gDiscoveryHeap->analogValueTpl[i])
        );
        snprintf(
            gDiscoveryHeap->analogStateSuffix[i],
            sizeof(gDiscoveryHeap->analogStateSuffix[i]),
            "rt/io/input/a%02u",
            (unsigned)i
        );
        if (spec.objectSuffix) {
            snprintf(
                gDiscoveryHeap->analogObjectSuffix[i],
                sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                "%s",
                spec.objectSuffix
            );
        } else {
            snprintf(
                gDiscoveryHeap->analogObjectSuffix[i],
                sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                "io_a%02u",
                (unsigned)i
            );
        }
        snprintf(
            gDiscoveryHeap->analogFallbackName[i],
            sizeof(gDiscoveryHeap->analogFallbackName[i]),
            "A%02u",
            (unsigned)i
        );
        char endpointId[8] = {0};
        snprintf(endpointId, sizeof(endpointId), "a%02u", (unsigned)i);
        const char* label = spec.name;
        if (!label || label[0] == '\0') {
            label = modules.ioModule.endpointLabel(endpointId);
        }
        if (!label || label[0] == '\0') {
            label = gDiscoveryHeap->analogFallbackName[i];
        }
        const HASensorEntry entry{
            "io",
            gDiscoveryHeap->analogObjectSuffix[i],
            label,
            gDiscoveryHeap->analogStateSuffix[i],
            gDiscoveryHeap->analogValueTpl[i],
            nullptr,
            spec.icon,
            spec.unit,
            false,
            kAvailabilityTpl
        };
        (void)modules.haService->addSensor(modules.haService->ctx, &entry);
    }
}

void syncDigitalInputBinarySensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addBinarySensor || !modules.haService->addSensor) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");
    static constexpr const char* kBoolTpl = "{{ 'True' if value_json.value else 'False' }}";
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";
    static constexpr const char* kNumericTpl =
        "{% if value_json.value is number %}{{ value_json.value | float }}{% else %}unavailable{% endif %}";

    for (uint8_t i = 0; i < (uint8_t)(sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])); ++i) {
        const FlowIoDigitalHaSpec& spec = kDigitalHaSpecs[i];
        if (!modules.ioModule.digitalInputSlotUsed(spec.logicalIdx)) continue;

        snprintf(
            gDiscoveryHeap->digitalStateSuffix[i],
            sizeof(gDiscoveryHeap->digitalStateSuffix[i]),
            "rt/io/input/i%02u",
            (unsigned)spec.logicalIdx
        );
        if (modules.ioModule.digitalInputValueType(spec.logicalIdx) != IO_VAL_BOOL) {
            const HASensorEntry entry{
                "io",
                spec.objectSuffix,
                spec.name,
                gDiscoveryHeap->digitalStateSuffix[i],
                kNumericTpl,
                nullptr,
                spec.icon,
                spec.unit,
                false,
                kAvailabilityTpl
            };
            (void)modules.haService->addSensor(modules.haService->ctx, &entry);
            continue;
        }

        const HABinarySensorEntry entry{
            "io",
            spec.objectSuffix,
            spec.name,
            gDiscoveryHeap->digitalStateSuffix[i],
            kBoolTpl,
            nullptr,
            nullptr,
            spec.icon
        };
        (void)modules.haService->addBinarySensor(modules.haService->ctx, &entry);
    }
}

void syncSwitches(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSwitch) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");

    for (uint8_t i = 0; i < PoolBinding::kDeviceBindingCount; ++i) {
        const PoolIoBinding& binding = PoolBinding::kIoBindings[i];
        if (binding.ioId < IO_ID_DO_BASE) continue;

        const uint8_t logical = (uint8_t)(binding.ioId - IO_ID_DO_BASE);
        if (!modules.ioModule.digitalOutputSlotUsed(logical)) continue;

        snprintf(
            gDiscoveryHeap->switchStateSuffix[i],
            sizeof(gDiscoveryHeap->switchStateSuffix[i]),
            "rt/io/output/d%02u",
            (unsigned)logical
        );
        bool payloadOk = true;

        if (binding.slot == PoolBinding::kDeviceSlotFiltrationPump) {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":true}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":false}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        } else {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":true}}",
                (unsigned)binding.slot
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":false}}",
                (unsigned)binding.slot
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        }

        if (!payloadOk) {
            requireSetup(false, "ha switch payload");
            continue;
        }

        const HASwitchEntry entry{
            "io",
            binding.objectSuffix,
            binding.name,
            gDiscoveryHeap->switchStateSuffix[i],
            "{% if value_json.value %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCmd,
            gDiscoveryHeap->switchPayloadOn[i],
            gDiscoveryHeap->switchPayloadOff[i],
            binding.haIcon,
            nullptr
        };
        (void)modules.haService->addSwitch(modules.haService->ctx, &entry);
    }
}

}  // namespace

namespace Profiles {
namespace FlowIOS3 {

void configureIoModule(const AppContext& ctx, ModuleInstances& modules)
{
    requireSetup(ctx.domain != nullptr, "missing domain spec");

    modules.ioModule.setOneWireBuses(&modules.oneWireWater, &modules.oneWireAir);
    modules.ioModule.setBindingPorts(
        FlowIoLayout::kBindingPorts,
        (uint8_t)(sizeof(FlowIoLayout::kBindingPorts) / sizeof(FlowIoLayout::kBindingPorts[0]))
    );

    for (uint8_t i = 0; i < ctx.domain->sensorCount; ++i) {
        const DomainSensorPreset& preset = ctx.domain->sensors[i];
        const PoolSensorBinding* compat = PoolBinding::sensorBindingBySlot(preset.legacySlot);
        requireSetup(compat != nullptr, "missing compatibility sensor binding");

        if (preset.digitalInput) {
            IODigitalInputDefinition def{};
            snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
            def.ioId = compat->ioId;
            def.activeHigh = false;
            def.pullMode = IO_PULL_UP;
            applyDigitalDefaultsForRole(preset.role, def);
            const uint8_t diOrdinal = digitalInputOrdinalFromPort(def.bindingPort);
            if (diOrdinal != 0U) {
                snprintf(def.id, sizeof(def.id), "DI Pin %u", (unsigned)diOrdinal);
            }
            requireSetup(modules.ioModule.defineDigitalInput(def), "define digital input");
            continue;
        }

        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
        def.ioId = compat->ioId;
        applyAnalogDefaultsForRole(preset.role, def);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define analog input");
    }

    for (uint8_t i = 4; i < 8; ++i) {
        IODigitalInputDefinition def{};
        snprintf(def.id, sizeof(def.id), "DI Pin %u", (unsigned)(i + 1));
        def.ioId = (IoId)(IO_ID_DI_BASE + i);
        def.activeHigh = false;
        def.pullMode = IO_PULL_UP;
        def.mode = IO_DIGITAL_INPUT_STATE;
        def.edgeMode = IO_EDGE_RISING;
        def.counterDebounceUs = 0U;
        def.bindingPort = (i == 4) ? FlowIoLayout::PortDigitalIn5 :
                         (i == 5) ? FlowIoLayout::PortDigitalIn6 :
                         (i == 6) ? FlowIoLayout::PortDigitalIn7 :
                                    FlowIoLayout::PortDigitalIn8;
        requireSetup(modules.ioModule.defineDigitalInput(def), "define extra digital input");
    }

    for (uint8_t i = 6; i < 11; ++i) {
        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "a%02u", (unsigned)i);
        def.ioId = (IoId)(IO_ID_AI_BASE + i);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define extra analog input");
    }

    for (uint8_t i = 0; i < ctx.domain->poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = ctx.domain->poolDevices[i];
        const PoolIoBinding* compat = PoolBinding::ioBindingBySlot(preset.legacySlot);
        requireSetup(compat != nullptr, "missing compatibility output binding");
        const FlowIoLayout::DigitalOutputRoleDefault* spec = FlowIoLayout::digitalOutputDefaultForRole(preset.role);
        requireSetup(spec != nullptr, "missing output layout binding");

        IODigitalOutputDefinition def{};
        const uint8_t exioOrdinal = exioOrdinalFromPort(spec->bindingPort);
        if (exioOrdinal != 0U) {
            snprintf(def.id, sizeof(def.id), "EXIO%u", (unsigned)exioOrdinal);
        } else {
            snprintf(def.id, sizeof(def.id), "%s", compat->objectSuffix ? compat->objectSuffix : "output");
        }
        def.ioId = compat->ioId;
        def.bindingPort = spec->bindingPort;
        def.activeHigh = spec->activeHigh;
        def.initialOn = false;
        def.momentary = spec->momentary;
        def.pulseMs = spec->momentary ? spec->pulseMs : 0;
        requireSetup(modules.ioModule.defineDigitalOutput(def), "define digital output");
    }
}

void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    modules.haService = ctx.services.get<HAService>(ServiceId::Ha);
    if (!modules.haService) return;

    syncAnalogSensors(modules);
    syncDigitalInputBinarySensors(modules);
    syncSwitches(modules);

    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gOneShotRefreshBypassedLogged) {
        FLOWIOS3_HA_BOOT_TRACE("FlowIOS3 IO->HA dynamic refresh bypassed in one-shot mode");
        gOneShotRefreshBypassedLogged = true;
    }
    releaseDiscoveryHeapIfReady(modules);
    return;
#endif
    if (!modules.haService) return;
    const uint32_t dirtyMask = modules.ioModule.takeAnalogConfigDirtyMask();
    if (dirtyMask == 0) return;

    syncAnalogSensors(modules);
    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

void releaseIoHomeAssistantDiscoveryHeapIfDone(ModuleInstances& modules)
{
    releaseDiscoveryHeapIfReady(modules);
}

}  // namespace FlowIOS3
}  // namespace Profiles
