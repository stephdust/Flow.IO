#pragma once
/**
 * @file IOModule.h
 * @brief Unified IO module with endpoint registry and scheduler.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/RuntimeUi.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IODrivers/Bme680Driver.h"
#include "Modules/IOModule/IODrivers/Bmp280Driver.h"
#include "Modules/IOModule/IODrivers/Ds18b20Driver.h"
#include "Modules/IOModule/IODrivers/GpioCounterDriver.h"
#include "Modules/IOModule/IODrivers/GpioDriver.h"
#include "Modules/IOModule/IODrivers/Ina226Driver.h"
#include "Modules/IOModule/IODrivers/Pcf8574BitDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574Driver.h"
#include "Modules/IOModule/IODrivers/Sht40Driver.h"
#include "Modules/IOModule/IOEndpoints/AnalogSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalActuatorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/Pcf8574MaskEndpoint.h"
#include "Modules/IOModule/IOEndpoints/RunningMedianAverageFloat.h"
#include "Modules/IOModule/IOModuleDataModel.h"
#include "Modules/IOModule/IOModuleTypes.h"
#include "Modules/IOModule/IOProviders/IOProviders.h"
#include "Modules/IOModule/IORegistry/IORegistry.h"
#include "Modules/IOModule/IOScheduler/IOScheduler.h"

class DataStore;
class OneWireBus;

class IOModule : public Module, public IRuntimeSnapshotProvider, public IRuntimeUiValueProvider {
public:
    ModuleId moduleId() const override { return ModuleId::Io; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "io"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2560; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

#if defined(FLOW_PROFILE_SUPERVISOR)
    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        return ModuleId::Unknown;
    }
#else
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::Mqtt;
        return ModuleId::Unknown;
    }
#endif

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void loop() override;

    void setOneWireBuses(OneWireBus* water, OneWireBus* air);
    void setBindingPorts(const IOBindingPortSpec* ports, uint8_t count);
    bool defineAnalogInput(const IOAnalogDefinition& def);
    bool defineDigitalInput(const IODigitalInputDefinition& def);
    bool defineDigitalOutput(const IODigitalOutputDefinition& def);
    const char* analogSlotName(uint8_t idx) const;
    bool analogSlotUsed(uint8_t idx) const;
    bool analogSlotPublished(uint8_t idx) const;
    bool digitalInputSlotUsed(uint8_t logicalIdx) const;
    uint8_t digitalInputValueType(uint8_t logicalIdx) const;
    int32_t digitalInputPrecision(uint8_t logicalIdx) const;
    bool digitalOutputSlotUsed(uint8_t logicalIdx) const;
    int32_t analogPrecision(uint8_t idx) const;
    uint16_t takeAnalogConfigDirtyMask();
    const char* endpointLabel(const char* endpointId) const;
    bool buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

    IORegistry& registry() { return registry_; }

private:
    struct AnalogSlot;
    struct DigitalSlot;

    enum RuntimeUiValueId : uint8_t {
        RuntimeUiWaterTemp = 1,
        RuntimeUiAirTemp = 2,
        RuntimeUiPh = 3,
        RuntimeUiOrp = 4,
        RuntimeUiWaterCounter = 5,
    };

    static bool tickFastAds_(void* ctx, uint32_t nowMs);
    static bool tickSlowDs_(void* ctx, uint32_t nowMs);
    static bool tickI2cAnalogs_(void* ctx, uint32_t nowMs);
    static bool tickDigitalInputs_(void* ctx, uint32_t nowMs);

    uint8_t ioCount_() const;
    IoStatus ioIdAt_(uint8_t index, IoId* outId) const;
    IoStatus ioMeta_(IoId id, IoEndpointMeta* outMeta) const;
    IoStatus ioReadValue_(IoId id, IoValue* outValue) const;
    IoStatus ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs);
    IoStatus ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioTick_(uint32_t nowMs);
    IoStatus ioLastCycle_(IoCycleInfo* outCycle) const;

    bool setLedMask_(uint8_t mask, uint32_t tsMs);
    bool turnLedOn_(uint8_t bit, uint32_t tsMs);
    bool turnLedOff_(uint8_t bit, uint32_t tsMs);
    bool getLedMask_(uint8_t& mask) const;
    bool getLedMaskSvc_(uint8_t* mask) const;
    uint8_t pcfPhysicalFromLogical_(uint8_t logicalMask) const;
    uint8_t pcfLogicalFromPhysical_(uint8_t physicalMask) const;

    bool configureRuntime_();
    const IOBindingPortSpec* bindingPortSpec_(PhysicalPortId portId) const;
    bool resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const;
    bool resolveDigitalInputBinding_(PhysicalPortId portId, uint8_t& pinOut, uint8_t& backendOut, uint8_t& channelOut) const;
    bool resolveDigitalOutputBinding_(PhysicalPortId portId,
                                      uint8_t& pinOut,
                                      uint8_t& backendOut,
                                      uint8_t& channelOut,
                                      bool& usesPcfOut) const;
    bool resolveDsBusAddress_(OneWireBus* bus, const char* runtimeKey, uint8_t outAddr[8]);
    bool runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const;
    bool buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut, bool invalidAsUndefined = false) const;
    bool buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const;
    const IOAnalogProvider* analogProviderForSource_(uint8_t source) const;
    bool resolveConfiguredAnalogSource_(uint8_t idx, uint8_t& sourceOut) const;
    bool analogSourceRequiresDriverEnable_(uint8_t source) const;
    bool analogSourceDriverEnabled_(uint8_t source) const;
    bool analogSlotPublished_(uint8_t idx) const;
    bool analogSlotUsesUndefinedInvalidValue_(uint8_t idx) const;
    void invalidateAnalogSlot_(AnalogSlot& slot, uint32_t nowMs);
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs);
    int32_t sanitizeAnalogPrecision_(int32_t precision) const;
    void forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs);
    void refreshAnalogConfigState_();
    bool ensureAnalogPrecisionState_();
    bool ensureExtraAnalogCfgVars_();
    bool ensureDigitalCounterCfgVars_();
    bool ensureDigitalCounterConfigState_();
    bool ensureLastCycleState_();
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    bool digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const;
    bool findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const;
    bool findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const;
    ConfigVariable<float,0>* counterTotalVar_(uint8_t logicalIdx);
    float* counterConfigTotalState_(uint8_t logicalIdx);
    void eraseLegacyCounterPersistedTotal_(uint8_t logicalIdx);
    bool persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t rawCount);
    void traceDigitalCounters_(uint32_t nowMs);
    void beginIoCycle_(uint32_t nowMs);
    void markIoCycleChanged_(IoId id);
    static bool writeDigitalOut_(void* ctx, bool on);
    void pollPulseOutputs_(uint32_t nowMs);
    AnalogSensorEndpoint* allocAnalogEndpoint_(const char* endpointId);
    DigitalSensorEndpoint* allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType);
    DigitalActuatorEndpoint* allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx);
    IDigitalCounterDriver* allocGpioDriver_(const char* driverId,
                                            uint8_t pin,
                                            bool output,
                                            bool activeHigh,
                                            uint8_t inputPullMode = GpioDriver::PullNone,
                                            bool counterEnabled = false,
                                            uint8_t edgeMode = IO_EDGE_RISING,
                                            uint32_t counterDebounceUs = 0);
    IAnalogSourceDriver* allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg);
    IAnalogSourceDriver* allocDsDriver_(const char* driverId, OneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg);
    IAnalogSourceDriver* allocSht40Driver_(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg);
    IAnalogSourceDriver* allocBmp280Driver_(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg);
    IAnalogSourceDriver* allocBme680Driver_(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg);
    IAnalogSourceDriver* allocIna226Driver_(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg);
    IDigitalPinDriver* allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh);
    IMaskOutputDriver* allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    Pcf8574MaskEndpoint* allocMaskEndpoint_(const char* endpointId, MaskWriteFn writeFn, MaskReadFn readFn, void* fnCtx);

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = 15;
    static constexpr uint8_t MAX_DIGITAL_INPUTS = 5;
    static constexpr uint8_t MAX_DIGITAL_OUTPUTS = 10;
    static constexpr uint8_t MAX_DIGITAL_SLOTS = MAX_DIGITAL_INPUTS + MAX_DIGITAL_OUTPUTS;
    static constexpr uint8_t ANALOG_CFG_SLOTS = MAX_ANALOG_ENDPOINTS;
    static constexpr uint8_t DIGITAL_CFG_SLOTS = MAX_DIGITAL_OUTPUTS;
    /** End-exclusive upper bounds for each static id range. */
    static constexpr IoId IO_ID_DO_MAX = IO_ID_DO_BASE + MAX_DIGITAL_OUTPUTS;
    static constexpr IoId IO_ID_DI_MAX = IO_ID_DI_BASE + MAX_DIGITAL_INPUTS;
    static constexpr IoId IO_ID_AI_MAX = IO_ID_AI_BASE + MAX_ANALOG_ENDPOINTS;

    struct AnalogSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOAnalogDefinition def{};
        // `source` identifies the shared physical driver; `channel` selects one logical measurement.
        uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
        uint8_t channel = 0;
        uint8_t backend = IO_BACKEND_ADS1115_INT;
        AnalogSensorEndpoint* endpoint = nullptr;
        RunningMedianAverageFloat median{11, 5};
        bool lastSampleSeqValid = false;
        uint32_t lastSampleSeq = 0;
        bool lastRoundedValid = false;
        float lastRounded = 0.0f;
    };
    enum DigitalSlotKind : uint8_t {
        DIGITAL_SLOT_INPUT = 0,
        DIGITAL_SLOT_OUTPUT = 1
    };
    struct DigitalSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOModule* owner = nullptr;
        uint8_t kind = DIGITAL_SLOT_INPUT;
        uint8_t logicalIdx = 0;
        char endpointId[8] = {0};
        IODigitalInputDefinition inDef{};
        IODigitalOutputDefinition outDef{};
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0;
        IODigitalProvider provider{};
        IOEndpoint* endpoint = nullptr;
        bool pulseArmed = false;
        uint32_t pulseDeadlineMs = 0;
        bool lastValid = false;
        bool lastValue = false;
        float counterScaledTotal = 0.0f;
        int32_t counterLastRawCount = 0;
        int32_t counterLastFlushedRawCount = 0;
    };

    struct ExtraAnalogConfigVars {
        ConfigVariable<char,0> a6NameVar_;
        ConfigVariable<PhysicalPortId,0> a6BindingVar_;
        ConfigVariable<float,0> a6C0Var_;
        ConfigVariable<float,0> a6C1Var_;
        ConfigVariable<int32_t,0> a6PrecVar_;
        ConfigVariable<char,0> a7NameVar_;
        ConfigVariable<PhysicalPortId,0> a7BindingVar_;
        ConfigVariable<float,0> a7C0Var_;
        ConfigVariable<float,0> a7C1Var_;
        ConfigVariable<int32_t,0> a7PrecVar_;
        ConfigVariable<char,0> a8NameVar_;
        ConfigVariable<PhysicalPortId,0> a8BindingVar_;
        ConfigVariable<float,0> a8C0Var_;
        ConfigVariable<float,0> a8C1Var_;
        ConfigVariable<int32_t,0> a8PrecVar_;
        ConfigVariable<char,0> a9NameVar_;
        ConfigVariable<PhysicalPortId,0> a9BindingVar_;
        ConfigVariable<float,0> a9C0Var_;
        ConfigVariable<float,0> a9C1Var_;
        ConfigVariable<int32_t,0> a9PrecVar_;
        ConfigVariable<char,0> a10NameVar_;
        ConfigVariable<PhysicalPortId,0> a10BindingVar_;
        ConfigVariable<float,0> a10C0Var_;
        ConfigVariable<float,0> a10C1Var_;
        ConfigVariable<int32_t,0> a10PrecVar_;
        ConfigVariable<char,0> a11NameVar_;
        ConfigVariable<PhysicalPortId,0> a11BindingVar_;
        ConfigVariable<float,0> a11C0Var_;
        ConfigVariable<float,0> a11C1Var_;
        ConfigVariable<int32_t,0> a11PrecVar_;
        ConfigVariable<char,0> a12NameVar_;
        ConfigVariable<PhysicalPortId,0> a12BindingVar_;
        ConfigVariable<float,0> a12C0Var_;
        ConfigVariable<float,0> a12C1Var_;
        ConfigVariable<int32_t,0> a12PrecVar_;
        ConfigVariable<char,0> a13NameVar_;
        ConfigVariable<PhysicalPortId,0> a13BindingVar_;
        ConfigVariable<float,0> a13C0Var_;
        ConfigVariable<float,0> a13C1Var_;
        ConfigVariable<int32_t,0> a13PrecVar_;
        ConfigVariable<char,0> a14NameVar_;
        ConfigVariable<PhysicalPortId,0> a14BindingVar_;
        ConfigVariable<float,0> a14C0Var_;
        ConfigVariable<float,0> a14C1Var_;
        ConfigVariable<int32_t,0> a14PrecVar_;

        explicit ExtraAnalogConfigVars(IOAnalogSlotConfig* analogCfg)
            : a6NameVar_{NVS_KEY(NvsKeys::Io::IO_A6NM), "a06_name", "io/input/a06", ConfigType::CharArray, (char*)analogCfg[6].name, ConfigPersistence::Persistent, sizeof(analogCfg[6].name)},
              a6BindingVar_{NVS_KEY(NvsKeys::Io::IO_A6BP), "binding_port", "io/input/a06", ConfigType::UInt16, &analogCfg[6].bindingPort, ConfigPersistence::Persistent, 0},
              a6C0Var_{NVS_KEY(NvsKeys::Io::IO_A60), "a06_c0", "io/input/a06", ConfigType::Float, &analogCfg[6].c0, ConfigPersistence::Persistent, 0},
              a6C1Var_{NVS_KEY(NvsKeys::Io::IO_A61), "a06_c1", "io/input/a06", ConfigType::Float, &analogCfg[6].c1, ConfigPersistence::Persistent, 0},
              a6PrecVar_{NVS_KEY(NvsKeys::Io::IO_A6P), "a06_prec", "io/input/a06", ConfigType::Int32, &analogCfg[6].precision, ConfigPersistence::Persistent, 0},
              a7NameVar_{NVS_KEY(NvsKeys::Io::IO_A7NM), "a07_name", "io/input/a07", ConfigType::CharArray, (char*)analogCfg[7].name, ConfigPersistence::Persistent, sizeof(analogCfg[7].name)},
              a7BindingVar_{NVS_KEY(NvsKeys::Io::IO_A7BP), "binding_port", "io/input/a07", ConfigType::UInt16, &analogCfg[7].bindingPort, ConfigPersistence::Persistent, 0},
              a7C0Var_{NVS_KEY(NvsKeys::Io::IO_A70), "a07_c0", "io/input/a07", ConfigType::Float, &analogCfg[7].c0, ConfigPersistence::Persistent, 0},
              a7C1Var_{NVS_KEY(NvsKeys::Io::IO_A71), "a07_c1", "io/input/a07", ConfigType::Float, &analogCfg[7].c1, ConfigPersistence::Persistent, 0},
              a7PrecVar_{NVS_KEY(NvsKeys::Io::IO_A7P), "a07_prec", "io/input/a07", ConfigType::Int32, &analogCfg[7].precision, ConfigPersistence::Persistent, 0},
              a8NameVar_{NVS_KEY(NvsKeys::Io::IO_A8NM), "a08_name", "io/input/a08", ConfigType::CharArray, (char*)analogCfg[8].name, ConfigPersistence::Persistent, sizeof(analogCfg[8].name)},
              a8BindingVar_{NVS_KEY(NvsKeys::Io::IO_A8BP), "binding_port", "io/input/a08", ConfigType::UInt16, &analogCfg[8].bindingPort, ConfigPersistence::Persistent, 0},
              a8C0Var_{NVS_KEY(NvsKeys::Io::IO_A80), "a08_c0", "io/input/a08", ConfigType::Float, &analogCfg[8].c0, ConfigPersistence::Persistent, 0},
              a8C1Var_{NVS_KEY(NvsKeys::Io::IO_A81), "a08_c1", "io/input/a08", ConfigType::Float, &analogCfg[8].c1, ConfigPersistence::Persistent, 0},
              a8PrecVar_{NVS_KEY(NvsKeys::Io::IO_A8P), "a08_prec", "io/input/a08", ConfigType::Int32, &analogCfg[8].precision, ConfigPersistence::Persistent, 0},
              a9NameVar_{NVS_KEY(NvsKeys::Io::IO_A9NM), "a09_name", "io/input/a09", ConfigType::CharArray, (char*)analogCfg[9].name, ConfigPersistence::Persistent, sizeof(analogCfg[9].name)},
              a9BindingVar_{NVS_KEY(NvsKeys::Io::IO_A9BP), "binding_port", "io/input/a09", ConfigType::UInt16, &analogCfg[9].bindingPort, ConfigPersistence::Persistent, 0},
              a9C0Var_{NVS_KEY(NvsKeys::Io::IO_A90), "a09_c0", "io/input/a09", ConfigType::Float, &analogCfg[9].c0, ConfigPersistence::Persistent, 0},
              a9C1Var_{NVS_KEY(NvsKeys::Io::IO_A91), "a09_c1", "io/input/a09", ConfigType::Float, &analogCfg[9].c1, ConfigPersistence::Persistent, 0},
              a9PrecVar_{NVS_KEY(NvsKeys::Io::IO_A9P), "a09_prec", "io/input/a09", ConfigType::Int32, &analogCfg[9].precision, ConfigPersistence::Persistent, 0},
              a10NameVar_{NVS_KEY(NvsKeys::Io::IO_A10NM), "a10_name", "io/input/a10", ConfigType::CharArray, (char*)analogCfg[10].name, ConfigPersistence::Persistent, sizeof(analogCfg[10].name)},
              a10BindingVar_{NVS_KEY(NvsKeys::Io::IO_A10BP), "binding_port", "io/input/a10", ConfigType::UInt16, &analogCfg[10].bindingPort, ConfigPersistence::Persistent, 0},
              a10C0Var_{NVS_KEY(NvsKeys::Io::IO_A100), "a10_c0", "io/input/a10", ConfigType::Float, &analogCfg[10].c0, ConfigPersistence::Persistent, 0},
              a10C1Var_{NVS_KEY(NvsKeys::Io::IO_A101), "a10_c1", "io/input/a10", ConfigType::Float, &analogCfg[10].c1, ConfigPersistence::Persistent, 0},
              a10PrecVar_{NVS_KEY(NvsKeys::Io::IO_A10P), "a10_prec", "io/input/a10", ConfigType::Int32, &analogCfg[10].precision, ConfigPersistence::Persistent, 0},
              a11NameVar_{NVS_KEY(NvsKeys::Io::IO_A11NM), "a11_name", "io/input/a11", ConfigType::CharArray, (char*)analogCfg[11].name, ConfigPersistence::Persistent, sizeof(analogCfg[11].name)},
              a11BindingVar_{NVS_KEY(NvsKeys::Io::IO_A11BP), "binding_port", "io/input/a11", ConfigType::UInt16, &analogCfg[11].bindingPort, ConfigPersistence::Persistent, 0},
              a11C0Var_{NVS_KEY(NvsKeys::Io::IO_A110), "a11_c0", "io/input/a11", ConfigType::Float, &analogCfg[11].c0, ConfigPersistence::Persistent, 0},
              a11C1Var_{NVS_KEY(NvsKeys::Io::IO_A111), "a11_c1", "io/input/a11", ConfigType::Float, &analogCfg[11].c1, ConfigPersistence::Persistent, 0},
              a11PrecVar_{NVS_KEY(NvsKeys::Io::IO_A11P), "a11_prec", "io/input/a11", ConfigType::Int32, &analogCfg[11].precision, ConfigPersistence::Persistent, 0},
              a12NameVar_{NVS_KEY(NvsKeys::Io::IO_A12NM), "a12_name", "io/input/a12", ConfigType::CharArray, (char*)analogCfg[12].name, ConfigPersistence::Persistent, sizeof(analogCfg[12].name)},
              a12BindingVar_{NVS_KEY(NvsKeys::Io::IO_A12BP), "binding_port", "io/input/a12", ConfigType::UInt16, &analogCfg[12].bindingPort, ConfigPersistence::Persistent, 0},
              a12C0Var_{NVS_KEY(NvsKeys::Io::IO_A120), "a12_c0", "io/input/a12", ConfigType::Float, &analogCfg[12].c0, ConfigPersistence::Persistent, 0},
              a12C1Var_{NVS_KEY(NvsKeys::Io::IO_A121), "a12_c1", "io/input/a12", ConfigType::Float, &analogCfg[12].c1, ConfigPersistence::Persistent, 0},
              a12PrecVar_{NVS_KEY(NvsKeys::Io::IO_A12P), "a12_prec", "io/input/a12", ConfigType::Int32, &analogCfg[12].precision, ConfigPersistence::Persistent, 0},
              a13NameVar_{NVS_KEY(NvsKeys::Io::IO_A13NM), "a13_name", "io/input/a13", ConfigType::CharArray, (char*)analogCfg[13].name, ConfigPersistence::Persistent, sizeof(analogCfg[13].name)},
              a13BindingVar_{NVS_KEY(NvsKeys::Io::IO_A13BP), "binding_port", "io/input/a13", ConfigType::UInt16, &analogCfg[13].bindingPort, ConfigPersistence::Persistent, 0},
              a13C0Var_{NVS_KEY(NvsKeys::Io::IO_A130), "a13_c0", "io/input/a13", ConfigType::Float, &analogCfg[13].c0, ConfigPersistence::Persistent, 0},
              a13C1Var_{NVS_KEY(NvsKeys::Io::IO_A131), "a13_c1", "io/input/a13", ConfigType::Float, &analogCfg[13].c1, ConfigPersistence::Persistent, 0},
              a13PrecVar_{NVS_KEY(NvsKeys::Io::IO_A13P), "a13_prec", "io/input/a13", ConfigType::Int32, &analogCfg[13].precision, ConfigPersistence::Persistent, 0},
              a14NameVar_{NVS_KEY(NvsKeys::Io::IO_A14NM), "a14_name", "io/input/a14", ConfigType::CharArray, (char*)analogCfg[14].name, ConfigPersistence::Persistent, sizeof(analogCfg[14].name)},
              a14BindingVar_{NVS_KEY(NvsKeys::Io::IO_A14BP), "binding_port", "io/input/a14", ConfigType::UInt16, &analogCfg[14].bindingPort, ConfigPersistence::Persistent, 0},
              a14C0Var_{NVS_KEY(NvsKeys::Io::IO_A140), "a14_c0", "io/input/a14", ConfigType::Float, &analogCfg[14].c0, ConfigPersistence::Persistent, 0},
              a14C1Var_{NVS_KEY(NvsKeys::Io::IO_A141), "a14_c1", "io/input/a14", ConfigType::Float, &analogCfg[14].c1, ConfigPersistence::Persistent, 0},
              a14PrecVar_{NVS_KEY(NvsKeys::Io::IO_A14P), "a14_prec", "io/input/a14", ConfigType::Int32, &analogCfg[14].precision, ConfigPersistence::Persistent, 0}
        {
        }
    };

    struct ExtraDigitalCounterConfigVars {
        // CFGDOC: {"label":"Cumul compteur","help":"Valeur cumulee editable de cette entree en mode compteur. Cette correction ecrase immediatement le total actuel du compteur, devient la nouvelle statistique long terme et est ecrite immediatement en NVS."}
        ConfigVariable<float,0> i0TotalVar_;
        // CFGDOC: {"label":"Cumul compteur","help":"Valeur cumulee editable de cette entree en mode compteur. Cette correction ecrase immediatement le total actuel du compteur, devient la nouvelle statistique long terme et est ecrite immediatement en NVS."}
        ConfigVariable<float,0> i1TotalVar_;
        // CFGDOC: {"label":"Cumul compteur","help":"Valeur cumulee editable de cette entree en mode compteur. Cette correction ecrase immediatement le total actuel du compteur, devient la nouvelle statistique long terme et est ecrite immediatement en NVS."}
        ConfigVariable<float,0> i2TotalVar_;
        // CFGDOC: {"label":"Cumul compteur","help":"Valeur cumulee editable de cette entree en mode compteur. Cette correction ecrase immediatement le total actuel du compteur, devient la nouvelle statistique long terme et est ecrite immediatement en NVS."}
        ConfigVariable<float,0> i3TotalVar_;
        // CFGDOC: {"label":"Cumul compteur","help":"Valeur cumulee editable de cette entree en mode compteur. Cette correction ecrase immediatement le total actuel du compteur, devient la nouvelle statistique long terme et est ecrite immediatement en NVS."}
        ConfigVariable<float,0> i4TotalVar_;

        explicit ExtraDigitalCounterConfigVars(IODigitalInputSlotConfig* digitalCfg)
            : i0TotalVar_{NVS_KEY(NvsKeys::Io::IO_I0CT), "counter_total", "io/input/i00", ConfigType::Float, &digitalCfg[0].counterTotal, ConfigPersistence::Persistent, 0},
              i1TotalVar_{NVS_KEY(NvsKeys::Io::IO_I1CT), "counter_total", "io/input/i01", ConfigType::Float, &digitalCfg[1].counterTotal, ConfigPersistence::Persistent, 0},
              i2TotalVar_{NVS_KEY(NvsKeys::Io::IO_I2CT), "counter_total", "io/input/i02", ConfigType::Float, &digitalCfg[2].counterTotal, ConfigPersistence::Persistent, 0},
              i3TotalVar_{NVS_KEY(NvsKeys::Io::IO_I3CT), "counter_total", "io/input/i03", ConfigType::Float, &digitalCfg[3].counterTotal, ConfigPersistence::Persistent, 0},
              i4TotalVar_{NVS_KEY(NvsKeys::Io::IO_I4CT), "counter_total", "io/input/i04", ConfigType::Float, &digitalCfg[4].counterTotal, ConfigPersistence::Persistent, 0}
        {
        }
    };

    IOModuleConfig cfgData_{};
    IOAnalogSlotConfig analogCfg_[ANALOG_CFG_SLOTS]{};
    IODigitalInputSlotConfig digitalInCfg_[MAX_DIGITAL_INPUTS]{};
    IODigitalOutputSlotConfig digitalCfg_[DIGITAL_CFG_SLOTS]{};
    const IOBindingPortSpec* bindingPorts_ = nullptr;
    uint8_t bindingPortCount_ = 0;

    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    DataStore* dataStore_ = nullptr;
    MqttConfigRouteProducer cfgMqttPub_{};
    bool cfgMqttPubConfigured_ = false;

    IORegistry registry_{};
    IOScheduler scheduler_{};
    I2CBus i2cBus_{};

    OneWireBus* oneWireWater_ = nullptr;
    OneWireBus* oneWireAir_ = nullptr;
    uint8_t oneWireWaterAddr_[8] = {0};
    uint8_t oneWireAirAddr_[8] = {0};
    bool oneWireWaterAddrValid_ = false;
    bool oneWireAirAddrValid_ = false;

    IOAnalogProvider analogProviders_[IO_SRC_COUNT]{};
    IOMaskProvider ledMaskProvider_{};
    Pcf8574MaskEndpoint* ledMaskEp_ = nullptr;
    Pcf8574Driver* pcfDriver_ = nullptr;
    IOServiceV2 ioSvc_{
        ServiceBinding::bind<&IOModule::ioCount_>,
        ServiceBinding::bind<&IOModule::ioIdAt_>,
        ServiceBinding::bind<&IOModule::ioMeta_>,
        ServiceBinding::bind<&IOModule::ioReadValue_>,
        ServiceBinding::bind<&IOModule::ioReadDigital_>,
        ServiceBinding::bind<&IOModule::ioWriteDigital_>,
        ServiceBinding::bind<&IOModule::ioReadAnalog_>,
        ServiceBinding::bind<&IOModule::ioTick_>,
        ServiceBinding::bind<&IOModule::ioLastCycle_>,
        this
    };
    StatusLedsService statusLedsSvc_{
        ServiceBinding::bind<&IOModule::setLedMask_>,
        ServiceBinding::bind<&IOModule::getLedMaskSvc_>,
        this
    };
    bool pcfLastEnabled_ = false;
    uint8_t pcfLogicalMask_ = 0;
    bool pcfLogicalValid_ = false;
    IoCycleInfo* lastCycle_ = nullptr;

    AnalogSlot analogSlots_[MAX_ANALOG_ENDPOINTS]{};
    DigitalSlot digitalSlots_[MAX_DIGITAL_SLOTS]{};
    alignas(AnalogSensorEndpoint) uint8_t analogEndpointPool_[MAX_ANALOG_ENDPOINTS][sizeof(AnalogSensorEndpoint)]{};
    alignas(DigitalSensorEndpoint) uint8_t digitalSensorEndpointPool_[MAX_DIGITAL_INPUTS][sizeof(DigitalSensorEndpoint)]{};
    alignas(DigitalActuatorEndpoint) uint8_t digitalActuatorEndpointPool_[MAX_DIGITAL_OUTPUTS][sizeof(DigitalActuatorEndpoint)]{};
    alignas(GpioDriver) uint8_t gpioDriverPool_[MAX_DIGITAL_SLOTS][sizeof(GpioDriver)]{};
    alignas(GpioCounterDriver) uint8_t gpioCounterDriverPool_[MAX_DIGITAL_INPUTS][sizeof(GpioCounterDriver)]{};
    alignas(Ads1115Driver) uint8_t adsDriverPool_[2][sizeof(Ads1115Driver)]{};
    alignas(Ds18b20Driver) uint8_t dsDriverPool_[2][sizeof(Ds18b20Driver)]{};
    alignas(Sht40Driver) uint8_t sht40DriverPool_[1][sizeof(Sht40Driver)]{};
    alignas(Bmp280Driver) uint8_t bmp280DriverPool_[1][sizeof(Bmp280Driver)]{};
    alignas(Bme680Driver) uint8_t bme680DriverPool_[1][sizeof(Bme680Driver)]{};
    alignas(Ina226Driver) uint8_t ina226DriverPool_[1][sizeof(Ina226Driver)]{};
    alignas(Pcf8574Driver) uint8_t pcfDriverPool_[1][sizeof(Pcf8574Driver)]{};
    alignas(Pcf8574MaskEndpoint) uint8_t maskEndpointPool_[1][sizeof(Pcf8574MaskEndpoint)]{};
    uint8_t analogEndpointPoolUsed_ = 0;
    uint8_t digitalSensorEndpointPoolUsed_ = 0;
    uint8_t digitalActuatorEndpointPoolUsed_ = 0;
    uint8_t gpioDriverPoolUsed_ = 0;
    uint8_t gpioCounterDriverPoolUsed_ = 0;
    uint8_t pcfBitDriverPoolUsed_ = 0;
    uint8_t adsDriverPoolUsed_ = 0;
    uint8_t dsDriverPoolUsed_ = 0;
    uint8_t sht40DriverPoolUsed_ = 0;
    uint8_t bmp280DriverPoolUsed_ = 0;
    uint8_t bme680DriverPoolUsed_ = 0;
    uint8_t ina226DriverPoolUsed_ = 0;
    uint8_t pcfDriverPoolUsed_ = 0;
    uint8_t maskEndpointPoolUsed_ = 0;
    bool runtimeReady_ = false;
    bool runtimeInitAttempted_ = false;
    bool pcfEnableNeedsReinitWarned_ = false;
    uint32_t counterTraceLastMs_ = 0;
    uint32_t analogCalcLogLastMs_[3]{0, 0, 0};
    int32_t* analogPrecisionLast_ = nullptr;
    float* digitalCounterLastConfigTotals_ = nullptr;
    bool analogPrecisionLastInit_ = false;
    uint16_t analogConfigDirtyMask_ = 0;
    ExtraAnalogConfigVars* extraAnalogCfgVars_ = nullptr;
    ExtraDigitalCounterConfigVars* extraDigitalCounterCfgVars_ = nullptr;

    ConfigVariable<bool,0> enabledVar_ { NVS_KEY(NvsKeys::Io::IO_EN),"enabled","io",ConfigType::Bool,&cfgData_.enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA),"sda","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL),"scl","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsPollVar_ { NVS_KEY(NvsKeys::Io::IO_ADS),"poll_ms","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> dsPollVar_ { NVS_KEY(NvsKeys::Io::IO_DS),"poll_ms","io/drivers/ds18b20",ConfigType::Int32,&cfgData_.dsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> digitalPollVar_ { NVS_KEY(NvsKeys::Io::IO_DIN),"poll_ms","io/drivers/gpio",ConfigType::Int32,&cfgData_.digitalPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsInternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AIAD),"address","io/drivers/ads1115_int",ConfigType::UInt8,&cfgData_.adsInternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsExternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AEAD),"address","io/drivers/ads1115_ext",ConfigType::UInt8,&cfgData_.adsExternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsGainVar_ { NVS_KEY(NvsKeys::Io::IO_AGAI),"gain","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsGain,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsRateVar_ { NVS_KEY(NvsKeys::Io::IO_ARAT),"rate","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsRate,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> sht40EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_SHTEN),"enabled","io/drivers/sht40",ConfigType::Bool,&cfgData_.sht40Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> sht40AddressVar_ { NVS_KEY(NvsKeys::Io::IO_SHTAD),"address","io/drivers/sht40",ConfigType::UInt8,&cfgData_.sht40Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> sht40PollVar_ { NVS_KEY(NvsKeys::Io::IO_SHTPL),"poll_ms","io/drivers/sht40",ConfigType::Int32,&cfgData_.sht40PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bmp280EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMPEN),"enabled","io/drivers/bmp280",ConfigType::Bool,&cfgData_.bmp280Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bmp280AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMPAD),"address","io/drivers/bmp280",ConfigType::UInt8,&cfgData_.bmp280Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bmp280PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMPPL),"poll_ms","io/drivers/bmp280",ConfigType::Int32,&cfgData_.bmp280PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bme680EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMEEN),"enabled","io/drivers/bme680",ConfigType::Bool,&cfgData_.bme680Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bme680AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMEAD),"address","io/drivers/bme680",ConfigType::UInt8,&cfgData_.bme680Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bme680PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMEPL),"poll_ms","io/drivers/bme680",ConfigType::Int32,&cfgData_.bme680PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> ina226EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_INAEN),"enabled","io/drivers/ina226",ConfigType::Bool,&cfgData_.ina226Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> ina226AddressVar_ { NVS_KEY(NvsKeys::Io::IO_INAAD),"address","io/drivers/ina226",ConfigType::UInt8,&cfgData_.ina226Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> ina226PollVar_ { NVS_KEY(NvsKeys::Io::IO_INAPL),"poll_ms","io/drivers/ina226",ConfigType::Int32,&cfgData_.ina226PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<float,0> ina226ShuntOhmsVar_ { NVS_KEY(NvsKeys::Io::IO_INASH),"shunt_ohms","io/drivers/ina226",ConfigType::Float,&cfgData_.ina226ShuntOhms,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_PCFEN),"enabled","io/drivers/pcf857x",ConfigType::Bool,&cfgData_.pcfEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfAddressVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAD),"address","io/drivers/pcf857x",ConfigType::UInt8,&cfgData_.pcfAddress,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfMaskDefaultVar_ { NVS_KEY(NvsKeys::Io::IO_PCFMK),"mask_default","io/drivers/pcf857x",ConfigType::UInt8,&cfgData_.pcfMaskDefault,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfActiveLowVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAL),"active_low","io/drivers/pcf857x",ConfigType::Bool,&cfgData_.pcfActiveLow,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> traceEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_TREN),"trace_enabled","io/debug",ConfigType::Bool,&cfgData_.traceEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> tracePeriodVar_ { NVS_KEY(NvsKeys::Io::IO_TRMS),"trace_period_ms","io/debug",ConfigType::Int32,&cfgData_.tracePeriodMs,ConfigPersistence::Persistent,0 };

#define FLOW_IO_ANALOG_CFG_DECL(INDEX, SLOT_STR, KEYNM, KEYBP, KEYC0, KEYC1, KEYP) \
    ConfigVariable<char,0> a##INDEX##NameVar_{NVS_KEY(NvsKeys::Io::KEYNM),"a" SLOT_STR "_name","io/input/a" SLOT_STR,ConfigType::CharArray,(char*)analogCfg_[INDEX].name,ConfigPersistence::Persistent,sizeof(analogCfg_[INDEX].name)}; \
    ConfigVariable<PhysicalPortId,0> a##INDEX##BindingVar_{NVS_KEY(NvsKeys::Io::KEYBP),"binding_port","io/input/a" SLOT_STR,ConfigType::UInt16,&analogCfg_[INDEX].bindingPort,ConfigPersistence::Persistent,0}; \
    ConfigVariable<float,0> a##INDEX##C0Var_{NVS_KEY(NvsKeys::Io::KEYC0),"a" SLOT_STR "_c0","io/input/a" SLOT_STR,ConfigType::Float,&analogCfg_[INDEX].c0,ConfigPersistence::Persistent,0}; \
    ConfigVariable<float,0> a##INDEX##C1Var_{NVS_KEY(NvsKeys::Io::KEYC1),"a" SLOT_STR "_c1","io/input/a" SLOT_STR,ConfigType::Float,&analogCfg_[INDEX].c1,ConfigPersistence::Persistent,0}; \
    ConfigVariable<int32_t,0> a##INDEX##PrecVar_{NVS_KEY(NvsKeys::Io::KEYP),"a" SLOT_STR "_prec","io/input/a" SLOT_STR,ConfigType::Int32,&analogCfg_[INDEX].precision,ConfigPersistence::Persistent,0};

    FLOW_IO_ANALOG_CFG_DECL(0, "00", IO_A0NM, IO_A0BP, IO_A00, IO_A01, IO_A0P)
    FLOW_IO_ANALOG_CFG_DECL(1, "01", IO_A1NM, IO_A1BP, IO_A10, IO_A11, IO_A1P)
    FLOW_IO_ANALOG_CFG_DECL(2, "02", IO_A2NM, IO_A2BP, IO_A20, IO_A21, IO_A2P)
    FLOW_IO_ANALOG_CFG_DECL(3, "03", IO_A3NM, IO_A3BP, IO_A30, IO_A31, IO_A3P)
    FLOW_IO_ANALOG_CFG_DECL(4, "04", IO_A4NM, IO_A4BP, IO_A40, IO_A41, IO_A4P)
    FLOW_IO_ANALOG_CFG_DECL(5, "05", IO_A5NM, IO_A5BP, IO_A50, IO_A51, IO_A5P)
#undef FLOW_IO_ANALOG_CFG_DECL

    ConfigVariable<char,0> i0NameVar_{NVS_KEY(NvsKeys::Io::IO_I0NM),"i00_name","io/input/i00",ConfigType::CharArray,(char*)digitalInCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[0].name)};
    // CFGDOC: {"label":"Port physique I00","help":"Identifiant du port physique utilise par l'entree digitale I00. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i0BindingVar_{NVS_KEY(NvsKeys::Io::IO_I0BP),"binding_port","io/input/i00",ConfigType::UInt16,&digitalInCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I0AH),"i00_active_high","io/input/i00",ConfigType::Bool,&digitalInCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i0PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I0PU),"i00_pull_mode","io/input/i00",ConfigType::UInt8,&digitalInCfg_[0].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I00","help":"Front a compter pour I00 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i0EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I0ED),"edge_mode","io/input/i00",ConfigType::UInt8,&digitalInCfg_[0].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i0C0Var_{NVS_KEY(NvsKeys::Io::IO_I0C0),"i00_c0","io/input/i00",ConfigType::Float,&digitalInCfg_[0].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i0PrecVar_{NVS_KEY(NvsKeys::Io::IO_I0P),"i00_prec","io/input/i00",ConfigType::Int32,&digitalInCfg_[0].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i1NameVar_{NVS_KEY(NvsKeys::Io::IO_I1NM),"i01_name","io/input/i01",ConfigType::CharArray,(char*)digitalInCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[1].name)};
    // CFGDOC: {"label":"Port physique I01","help":"Identifiant du port physique utilise par l'entree digitale I01. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i1BindingVar_{NVS_KEY(NvsKeys::Io::IO_I1BP),"binding_port","io/input/i01",ConfigType::UInt16,&digitalInCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I1AH),"i01_active_high","io/input/i01",ConfigType::Bool,&digitalInCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i1PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I1PU),"i01_pull_mode","io/input/i01",ConfigType::UInt8,&digitalInCfg_[1].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I01","help":"Front a compter pour I01 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i1EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I1ED),"edge_mode","io/input/i01",ConfigType::UInt8,&digitalInCfg_[1].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i1C0Var_{NVS_KEY(NvsKeys::Io::IO_I1C0),"i01_c0","io/input/i01",ConfigType::Float,&digitalInCfg_[1].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i1PrecVar_{NVS_KEY(NvsKeys::Io::IO_I1P),"i01_prec","io/input/i01",ConfigType::Int32,&digitalInCfg_[1].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i2NameVar_{NVS_KEY(NvsKeys::Io::IO_I2NM),"i02_name","io/input/i02",ConfigType::CharArray,(char*)digitalInCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[2].name)};
    // CFGDOC: {"label":"Port physique I02","help":"Identifiant du port physique utilise par l'entree digitale I02. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i2BindingVar_{NVS_KEY(NvsKeys::Io::IO_I2BP),"binding_port","io/input/i02",ConfigType::UInt16,&digitalInCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I2AH),"i02_active_high","io/input/i02",ConfigType::Bool,&digitalInCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i2PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I2PU),"i02_pull_mode","io/input/i02",ConfigType::UInt8,&digitalInCfg_[2].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I02","help":"Front a compter pour I02 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i2EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I2ED),"edge_mode","io/input/i02",ConfigType::UInt8,&digitalInCfg_[2].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i2C0Var_{NVS_KEY(NvsKeys::Io::IO_I2C0),"i02_c0","io/input/i02",ConfigType::Float,&digitalInCfg_[2].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i2PrecVar_{NVS_KEY(NvsKeys::Io::IO_I2P),"i02_prec","io/input/i02",ConfigType::Int32,&digitalInCfg_[2].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i3NameVar_{NVS_KEY(NvsKeys::Io::IO_I3NM),"i03_name","io/input/i03",ConfigType::CharArray,(char*)digitalInCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[3].name)};
    // CFGDOC: {"label":"Port physique I03","help":"Identifiant du port physique utilise par l'entree digitale I03. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i3BindingVar_{NVS_KEY(NvsKeys::Io::IO_I3BP),"binding_port","io/input/i03",ConfigType::UInt16,&digitalInCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I3AH),"i03_active_high","io/input/i03",ConfigType::Bool,&digitalInCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i3PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I3PU),"i03_pull_mode","io/input/i03",ConfigType::UInt8,&digitalInCfg_[3].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I03","help":"Front a compter pour I03 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i3EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I3ED),"edge_mode","io/input/i03",ConfigType::UInt8,&digitalInCfg_[3].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i3C0Var_{NVS_KEY(NvsKeys::Io::IO_I3C0),"i03_c0","io/input/i03",ConfigType::Float,&digitalInCfg_[3].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i3PrecVar_{NVS_KEY(NvsKeys::Io::IO_I3P),"i03_prec","io/input/i03",ConfigType::Int32,&digitalInCfg_[3].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i4NameVar_{NVS_KEY(NvsKeys::Io::IO_I4NM),"i04_name","io/input/i04",ConfigType::CharArray,(char*)digitalInCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[4].name)};
    // CFGDOC: {"label":"Port physique I04","help":"Identifiant du port physique utilise par l'entree digitale I04. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i4BindingVar_{NVS_KEY(NvsKeys::Io::IO_I4BP),"binding_port","io/input/i04",ConfigType::UInt16,&digitalInCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I4AH),"i04_active_high","io/input/i04",ConfigType::Bool,&digitalInCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i4PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I4PU),"i04_pull_mode","io/input/i04",ConfigType::UInt8,&digitalInCfg_[4].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I04","help":"Front a compter pour I04 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i4EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I4ED),"edge_mode","io/input/i04",ConfigType::UInt8,&digitalInCfg_[4].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i4C0Var_{NVS_KEY(NvsKeys::Io::IO_I4C0),"i04_c0","io/input/i04",ConfigType::Float,&digitalInCfg_[4].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i4PrecVar_{NVS_KEY(NvsKeys::Io::IO_I4P),"i04_prec","io/input/i04",ConfigType::Int32,&digitalInCfg_[4].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d0NameVar_{NVS_KEY(NvsKeys::Io::IO_D0NM),"d00_name","io/output/d00",ConfigType::CharArray,(char*)digitalCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[0].name)};
    // CFGDOC: {"label":"Port physique D00","help":"Identifiant du port physique pilote par la sortie D00. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d0BindingVar_{NVS_KEY(NvsKeys::Io::IO_D0BP),"binding_port","io/output/d00",ConfigType::UInt16,&digitalCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D0AH),"d00_active_high","io/output/d00",ConfigType::Bool,&digitalCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D0IN),"d00_initial_on","io/output/d00",ConfigType::Bool,&digitalCfg_[0].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D0MO),"d00_momentary","io/output/d00",ConfigType::Bool,&digitalCfg_[0].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d0PulseVar_{NVS_KEY(NvsKeys::Io::IO_D0PM),"d00_pulse_ms","io/output/d00",ConfigType::Int32,&digitalCfg_[0].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d1NameVar_{NVS_KEY(NvsKeys::Io::IO_D1NM),"d01_name","io/output/d01",ConfigType::CharArray,(char*)digitalCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[1].name)};
    // CFGDOC: {"label":"Port physique D01","help":"Identifiant du port physique pilote par la sortie D01. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d1BindingVar_{NVS_KEY(NvsKeys::Io::IO_D1BP),"binding_port","io/output/d01",ConfigType::UInt16,&digitalCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D1AH),"d01_active_high","io/output/d01",ConfigType::Bool,&digitalCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D1IN),"d01_initial_on","io/output/d01",ConfigType::Bool,&digitalCfg_[1].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D1MO),"d01_momentary","io/output/d01",ConfigType::Bool,&digitalCfg_[1].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d1PulseVar_{NVS_KEY(NvsKeys::Io::IO_D1PM),"d01_pulse_ms","io/output/d01",ConfigType::Int32,&digitalCfg_[1].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d2NameVar_{NVS_KEY(NvsKeys::Io::IO_D2NM),"d02_name","io/output/d02",ConfigType::CharArray,(char*)digitalCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[2].name)};
    // CFGDOC: {"label":"Port physique D02","help":"Identifiant du port physique pilote par la sortie D02. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d2BindingVar_{NVS_KEY(NvsKeys::Io::IO_D2BP),"binding_port","io/output/d02",ConfigType::UInt16,&digitalCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D2AH),"d02_active_high","io/output/d02",ConfigType::Bool,&digitalCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D2IN),"d02_initial_on","io/output/d02",ConfigType::Bool,&digitalCfg_[2].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D2MO),"d02_momentary","io/output/d02",ConfigType::Bool,&digitalCfg_[2].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d2PulseVar_{NVS_KEY(NvsKeys::Io::IO_D2PM),"d02_pulse_ms","io/output/d02",ConfigType::Int32,&digitalCfg_[2].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d3NameVar_{NVS_KEY(NvsKeys::Io::IO_D3NM),"d03_name","io/output/d03",ConfigType::CharArray,(char*)digitalCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[3].name)};
    // CFGDOC: {"label":"Port physique D03","help":"Identifiant du port physique pilote par la sortie D03. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d3BindingVar_{NVS_KEY(NvsKeys::Io::IO_D3BP),"binding_port","io/output/d03",ConfigType::UInt16,&digitalCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D3AH),"d03_active_high","io/output/d03",ConfigType::Bool,&digitalCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D3IN),"d03_initial_on","io/output/d03",ConfigType::Bool,&digitalCfg_[3].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D3MO),"d03_momentary","io/output/d03",ConfigType::Bool,&digitalCfg_[3].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d3PulseVar_{NVS_KEY(NvsKeys::Io::IO_D3PM),"d03_pulse_ms","io/output/d03",ConfigType::Int32,&digitalCfg_[3].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d4NameVar_{NVS_KEY(NvsKeys::Io::IO_D4NM),"d04_name","io/output/d04",ConfigType::CharArray,(char*)digitalCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[4].name)};
    // CFGDOC: {"label":"Port physique D04","help":"Identifiant du port physique pilote par la sortie D04. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d4BindingVar_{NVS_KEY(NvsKeys::Io::IO_D4BP),"binding_port","io/output/d04",ConfigType::UInt16,&digitalCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D4AH),"d04_active_high","io/output/d04",ConfigType::Bool,&digitalCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D4IN),"d04_initial_on","io/output/d04",ConfigType::Bool,&digitalCfg_[4].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D4MO),"d04_momentary","io/output/d04",ConfigType::Bool,&digitalCfg_[4].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d4PulseVar_{NVS_KEY(NvsKeys::Io::IO_D4PM),"d04_pulse_ms","io/output/d04",ConfigType::Int32,&digitalCfg_[4].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d5NameVar_{NVS_KEY(NvsKeys::Io::IO_D5NM),"d05_name","io/output/d05",ConfigType::CharArray,(char*)digitalCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[5].name)};
    // CFGDOC: {"label":"Port physique D05","help":"Identifiant du port physique pilote par la sortie D05. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d5BindingVar_{NVS_KEY(NvsKeys::Io::IO_D5BP),"binding_port","io/output/d05",ConfigType::UInt16,&digitalCfg_[5].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D5AH),"d05_active_high","io/output/d05",ConfigType::Bool,&digitalCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D5IN),"d05_initial_on","io/output/d05",ConfigType::Bool,&digitalCfg_[5].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D5MO),"d05_momentary","io/output/d05",ConfigType::Bool,&digitalCfg_[5].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d5PulseVar_{NVS_KEY(NvsKeys::Io::IO_D5PM),"d05_pulse_ms","io/output/d05",ConfigType::Int32,&digitalCfg_[5].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d6NameVar_{NVS_KEY(NvsKeys::Io::IO_D6NM),"d06_name","io/output/d06",ConfigType::CharArray,(char*)digitalCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[6].name)};
    // CFGDOC: {"label":"Port physique D06","help":"Identifiant du port physique pilote par la sortie D06. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d6BindingVar_{NVS_KEY(NvsKeys::Io::IO_D6BP),"binding_port","io/output/d06",ConfigType::UInt16,&digitalCfg_[6].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D6AH),"d06_active_high","io/output/d06",ConfigType::Bool,&digitalCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D6IN),"d06_initial_on","io/output/d06",ConfigType::Bool,&digitalCfg_[6].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D6MO),"d06_momentary","io/output/d06",ConfigType::Bool,&digitalCfg_[6].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d6PulseVar_{NVS_KEY(NvsKeys::Io::IO_D6PM),"d06_pulse_ms","io/output/d06",ConfigType::Int32,&digitalCfg_[6].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d7NameVar_{NVS_KEY(NvsKeys::Io::IO_D7NM),"d07_name","io/output/d07",ConfigType::CharArray,(char*)digitalCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[7].name)};
    // CFGDOC: {"label":"Port physique D07","help":"Identifiant du port physique pilote par la sortie D07. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d7BindingVar_{NVS_KEY(NvsKeys::Io::IO_D7BP),"binding_port","io/output/d07",ConfigType::UInt16,&digitalCfg_[7].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D7AH),"d07_active_high","io/output/d07",ConfigType::Bool,&digitalCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D7IN),"d07_initial_on","io/output/d07",ConfigType::Bool,&digitalCfg_[7].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D7MO),"d07_momentary","io/output/d07",ConfigType::Bool,&digitalCfg_[7].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d7PulseVar_{NVS_KEY(NvsKeys::Io::IO_D7PM),"d07_pulse_ms","io/output/d07",ConfigType::Int32,&digitalCfg_[7].pulseMs,ConfigPersistence::Persistent,0};
};
