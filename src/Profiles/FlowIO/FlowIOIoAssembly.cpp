#include "Profiles/FlowIO/FlowIOIoAssembly.h"
#include "Profiles/FlowIO/FlowIOIoLayout.h"

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
#include "Profiles/FlowIO/FlowIOProfile.h"

namespace {

using Profiles::FlowIO::ModuleInstances;
namespace FlowIoLayout = Profiles::FlowIO::IoLayout;
static constexpr uint8_t kFlowIoAnalogHaSlots = 15;

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

bool ensureDiscoveryHeap()
{
    if (gDiscoveryHeap) return true;
    gDiscoveryHeap = static_cast<FlowIoDiscoveryHeap*>(
        heap_caps_calloc(1, sizeof(FlowIoDiscoveryHeap), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    return gDiscoveryHeap != nullptr;
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
namespace FlowIO {

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
            def.activeHigh = preset.activeHigh;
            def.pullMode = preset.pullMode;
            applyDigitalDefaultsForRole(preset.role, def);
            requireSetup(modules.ioModule.defineDigitalInput(def), "define digital input");
            continue;
        }

        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
        def.ioId = compat->ioId;
        applyAnalogDefaultsForRole(preset.role, def);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define analog input");
    }

    for (uint8_t i = 6; i < 15; ++i) {
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
        snprintf(def.id, sizeof(def.id), "%s", compat->objectSuffix ? compat->objectSuffix : "output");
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
    if (!modules.haService) return;
    const uint16_t dirtyMask = modules.ioModule.takeAnalogConfigDirtyMask();
    if (dirtyMask == 0) return;

    syncAnalogSensors(modules);
    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

}  // namespace FlowIO
}  // namespace Profiles
