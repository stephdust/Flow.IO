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
    bool digitalInputSlotUsed(uint8_t logicalIdx) const;
    uint8_t digitalInputValueType(uint8_t logicalIdx) const;
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
    bool buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const;
    const IOAnalogProvider* analogProviderForSource_(uint8_t source) const;
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs);
    int32_t sanitizeAnalogPrecision_(int32_t precision) const;
    void forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs);
    void refreshAnalogConfigState_();
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    bool digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const;
    bool findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const;
    bool findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const;
    bool loadCounterPersistedTotal_(uint8_t logicalIdx, int32_t& totalOut) const;
    bool persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t totalCount);
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

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = 12;
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
        int32_t lastCount = 0;
        int32_t counterPersistedTotal = 0;
        int32_t counterLastFlushedTotal = 0;
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
    IoCycleInfo lastCycle_{};

    AnalogSlot analogSlots_[MAX_ANALOG_ENDPOINTS]{};
    DigitalSlot digitalSlots_[MAX_DIGITAL_SLOTS]{};
    alignas(AnalogSensorEndpoint) uint8_t analogEndpointPool_[MAX_ANALOG_ENDPOINTS][sizeof(AnalogSensorEndpoint)]{};
    alignas(DigitalSensorEndpoint) uint8_t digitalSensorEndpointPool_[MAX_DIGITAL_INPUTS][sizeof(DigitalSensorEndpoint)]{};
    alignas(DigitalActuatorEndpoint) uint8_t digitalActuatorEndpointPool_[MAX_DIGITAL_OUTPUTS][sizeof(DigitalActuatorEndpoint)]{};
    alignas(GpioDriver) uint8_t gpioDriverPool_[MAX_DIGITAL_SLOTS][sizeof(GpioDriver)]{};
    alignas(GpioCounterDriver) uint8_t gpioCounterDriverPool_[MAX_DIGITAL_INPUTS][sizeof(GpioCounterDriver)]{};
    alignas(Pcf8574BitDriver) uint8_t pcfBitDriverPool_[MAX_DIGITAL_OUTPUTS][sizeof(Pcf8574BitDriver)]{};
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
    uint32_t analogCalcLogLastMs_[3]{0, 0, 0};
    int32_t analogPrecisionLast_[ANALOG_CFG_SLOTS]{};
    bool analogPrecisionLastInit_ = false;
    uint16_t analogConfigDirtyMask_ = 0;

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

    ConfigVariable<char,0> a0NameVar_{NVS_KEY(NvsKeys::Io::IO_A0NM),"a0_name","io/input/a0",ConfigType::CharArray,(char*)analogCfg_[0].name,ConfigPersistence::Persistent,sizeof(analogCfg_[0].name)};
    // CFGDOC: {"label":"Port physique A0","help":"Identifiant du port physique utilise par l'entree analogique A0. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a0BindingVar_{NVS_KEY(NvsKeys::Io::IO_A0BP),"binding_port","io/input/a0",ConfigType::UInt16,&analogCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C0Var_{NVS_KEY(NvsKeys::Io::IO_A00),"a0_c0","io/input/a0",ConfigType::Float,&analogCfg_[0].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C1Var_{NVS_KEY(NvsKeys::Io::IO_A01),"a0_c1","io/input/a0",ConfigType::Float,&analogCfg_[0].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a0PrecVar_{NVS_KEY(NvsKeys::Io::IO_A0P),"a0_prec","io/input/a0",ConfigType::Int32,&analogCfg_[0].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a1NameVar_{NVS_KEY(NvsKeys::Io::IO_A1NM),"a1_name","io/input/a1",ConfigType::CharArray,(char*)analogCfg_[1].name,ConfigPersistence::Persistent,sizeof(analogCfg_[1].name)};
    // CFGDOC: {"label":"Port physique A1","help":"Identifiant du port physique utilise par l'entree analogique A1. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a1BindingVar_{NVS_KEY(NvsKeys::Io::IO_A1BP),"binding_port","io/input/a1",ConfigType::UInt16,&analogCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C0Var_{NVS_KEY(NvsKeys::Io::IO_A10),"a1_c0","io/input/a1",ConfigType::Float,&analogCfg_[1].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C1Var_{NVS_KEY(NvsKeys::Io::IO_A11),"a1_c1","io/input/a1",ConfigType::Float,&analogCfg_[1].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a1PrecVar_{NVS_KEY(NvsKeys::Io::IO_A1P),"a1_prec","io/input/a1",ConfigType::Int32,&analogCfg_[1].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a2NameVar_{NVS_KEY(NvsKeys::Io::IO_A2NM),"a2_name","io/input/a2",ConfigType::CharArray,(char*)analogCfg_[2].name,ConfigPersistence::Persistent,sizeof(analogCfg_[2].name)};
    // CFGDOC: {"label":"Port physique A2","help":"Identifiant du port physique utilise par l'entree analogique A2. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a2BindingVar_{NVS_KEY(NvsKeys::Io::IO_A2BP),"binding_port","io/input/a2",ConfigType::UInt16,&analogCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C0Var_{NVS_KEY(NvsKeys::Io::IO_A20),"a2_c0","io/input/a2",ConfigType::Float,&analogCfg_[2].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C1Var_{NVS_KEY(NvsKeys::Io::IO_A21),"a2_c1","io/input/a2",ConfigType::Float,&analogCfg_[2].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a2PrecVar_{NVS_KEY(NvsKeys::Io::IO_A2P),"a2_prec","io/input/a2",ConfigType::Int32,&analogCfg_[2].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a3NameVar_{NVS_KEY(NvsKeys::Io::IO_A3NM),"a3_name","io/input/a3",ConfigType::CharArray,(char*)analogCfg_[3].name,ConfigPersistence::Persistent,sizeof(analogCfg_[3].name)};
    // CFGDOC: {"label":"Port physique A3","help":"Identifiant du port physique utilise par l'entree analogique A3. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a3BindingVar_{NVS_KEY(NvsKeys::Io::IO_A3BP),"binding_port","io/input/a3",ConfigType::UInt16,&analogCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C0Var_{NVS_KEY(NvsKeys::Io::IO_A30),"a3_c0","io/input/a3",ConfigType::Float,&analogCfg_[3].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C1Var_{NVS_KEY(NvsKeys::Io::IO_A31),"a3_c1","io/input/a3",ConfigType::Float,&analogCfg_[3].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a3PrecVar_{NVS_KEY(NvsKeys::Io::IO_A3P),"a3_prec","io/input/a3",ConfigType::Int32,&analogCfg_[3].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a4NameVar_{NVS_KEY(NvsKeys::Io::IO_A4NM),"a4_name","io/input/a4",ConfigType::CharArray,(char*)analogCfg_[4].name,ConfigPersistence::Persistent,sizeof(analogCfg_[4].name)};
    // CFGDOC: {"label":"Port physique A4","help":"Identifiant du port physique utilise par l'entree analogique A4. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a4BindingVar_{NVS_KEY(NvsKeys::Io::IO_A4BP),"binding_port","io/input/a4",ConfigType::UInt16,&analogCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C0Var_{NVS_KEY(NvsKeys::Io::IO_A40),"a4_c0","io/input/a4",ConfigType::Float,&analogCfg_[4].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C1Var_{NVS_KEY(NvsKeys::Io::IO_A41),"a4_c1","io/input/a4",ConfigType::Float,&analogCfg_[4].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a4PrecVar_{NVS_KEY(NvsKeys::Io::IO_A4P),"a4_prec","io/input/a4",ConfigType::Int32,&analogCfg_[4].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a5NameVar_{NVS_KEY(NvsKeys::Io::IO_A5NM),"a5_name","io/input/a5",ConfigType::CharArray,(char*)analogCfg_[5].name,ConfigPersistence::Persistent,sizeof(analogCfg_[5].name)};
    // CFGDOC: {"label":"Port physique A5","help":"Identifiant du port physique utilise par l'entree analogique A5. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> a5BindingVar_{NVS_KEY(NvsKeys::Io::IO_A5BP),"binding_port","io/input/a5",ConfigType::UInt16,&analogCfg_[5].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5C0Var_{NVS_KEY(NvsKeys::Io::IO_A50),"a5_c0","io/input/a5",ConfigType::Float,&analogCfg_[5].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5C1Var_{NVS_KEY(NvsKeys::Io::IO_A51),"a5_c1","io/input/a5",ConfigType::Float,&analogCfg_[5].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a5PrecVar_{NVS_KEY(NvsKeys::Io::IO_A5P),"a5_prec","io/input/a5",ConfigType::Int32,&analogCfg_[5].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i0NameVar_{NVS_KEY(NvsKeys::Io::IO_I0NM),"i0_name","io/input/i0",ConfigType::CharArray,(char*)digitalInCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[0].name)};
    // CFGDOC: {"label":"Port physique I0","help":"Identifiant du port physique utilise par l'entree digitale I0. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i0BindingVar_{NVS_KEY(NvsKeys::Io::IO_I0BP),"binding_port","io/input/i0",ConfigType::UInt16,&digitalInCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I0AH),"i0_active_high","io/input/i0",ConfigType::Bool,&digitalInCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i0PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I0PU),"i0_pull_mode","io/input/i0",ConfigType::UInt8,&digitalInCfg_[0].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I0","help":"Front a compter pour I0 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i0EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I0ED),"edge_mode","io/input/i0",ConfigType::UInt8,&digitalInCfg_[0].edgeMode,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i1NameVar_{NVS_KEY(NvsKeys::Io::IO_I1NM),"i1_name","io/input/i1",ConfigType::CharArray,(char*)digitalInCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[1].name)};
    // CFGDOC: {"label":"Port physique I1","help":"Identifiant du port physique utilise par l'entree digitale I1. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i1BindingVar_{NVS_KEY(NvsKeys::Io::IO_I1BP),"binding_port","io/input/i1",ConfigType::UInt16,&digitalInCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I1AH),"i1_active_high","io/input/i1",ConfigType::Bool,&digitalInCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i1PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I1PU),"i1_pull_mode","io/input/i1",ConfigType::UInt8,&digitalInCfg_[1].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I1","help":"Front a compter pour I1 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i1EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I1ED),"edge_mode","io/input/i1",ConfigType::UInt8,&digitalInCfg_[1].edgeMode,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i2NameVar_{NVS_KEY(NvsKeys::Io::IO_I2NM),"i2_name","io/input/i2",ConfigType::CharArray,(char*)digitalInCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[2].name)};
    // CFGDOC: {"label":"Port physique I2","help":"Identifiant du port physique utilise par l'entree digitale I2. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i2BindingVar_{NVS_KEY(NvsKeys::Io::IO_I2BP),"binding_port","io/input/i2",ConfigType::UInt16,&digitalInCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I2AH),"i2_active_high","io/input/i2",ConfigType::Bool,&digitalInCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i2PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I2PU),"i2_pull_mode","io/input/i2",ConfigType::UInt8,&digitalInCfg_[2].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I2","help":"Front a compter pour I2 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i2EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I2ED),"edge_mode","io/input/i2",ConfigType::UInt8,&digitalInCfg_[2].edgeMode,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i3NameVar_{NVS_KEY(NvsKeys::Io::IO_I3NM),"i3_name","io/input/i3",ConfigType::CharArray,(char*)digitalInCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[3].name)};
    // CFGDOC: {"label":"Port physique I3","help":"Identifiant du port physique utilise par l'entree digitale I3. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i3BindingVar_{NVS_KEY(NvsKeys::Io::IO_I3BP),"binding_port","io/input/i3",ConfigType::UInt16,&digitalInCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I3AH),"i3_active_high","io/input/i3",ConfigType::Bool,&digitalInCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i3PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I3PU),"i3_pull_mode","io/input/i3",ConfigType::UInt8,&digitalInCfg_[3].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I3","help":"Front a compter pour I3 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i3EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I3ED),"edge_mode","io/input/i3",ConfigType::UInt8,&digitalInCfg_[3].edgeMode,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i4NameVar_{NVS_KEY(NvsKeys::Io::IO_I4NM),"i4_name","io/input/i4",ConfigType::CharArray,(char*)digitalInCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[4].name)};
    // CFGDOC: {"label":"Port physique I4","help":"Identifiant du port physique utilise par l'entree digitale I4. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> i4BindingVar_{NVS_KEY(NvsKeys::Io::IO_I4BP),"binding_port","io/input/i4",ConfigType::UInt16,&digitalInCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I4AH),"i4_active_high","io/input/i4",ConfigType::Bool,&digitalInCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i4PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I4PU),"i4_pull_mode","io/input/i4",ConfigType::UInt8,&digitalInCfg_[4].pullMode,ConfigPersistence::Persistent,0};
    // CFGDOC: {"label":"Mode de front I4","help":"Front a compter pour I4 en mode compteur (0=descendant, 1=montant, 2=les deux)."}
    ConfigVariable<uint8_t,0> i4EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I4ED),"edge_mode","io/input/i4",ConfigType::UInt8,&digitalInCfg_[4].edgeMode,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d0NameVar_{NVS_KEY(NvsKeys::Io::IO_D0NM),"d0_name","io/output/d0",ConfigType::CharArray,(char*)digitalCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[0].name)};
    // CFGDOC: {"label":"Port physique D0","help":"Identifiant du port physique pilote par la sortie D0. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d0BindingVar_{NVS_KEY(NvsKeys::Io::IO_D0BP),"binding_port","io/output/d0",ConfigType::UInt16,&digitalCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D0AH),"d0_active_high","io/output/d0",ConfigType::Bool,&digitalCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D0IN),"d0_initial_on","io/output/d0",ConfigType::Bool,&digitalCfg_[0].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D0MO),"d0_momentary","io/output/d0",ConfigType::Bool,&digitalCfg_[0].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d0PulseVar_{NVS_KEY(NvsKeys::Io::IO_D0PM),"d0_pulse_ms","io/output/d0",ConfigType::Int32,&digitalCfg_[0].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d1NameVar_{NVS_KEY(NvsKeys::Io::IO_D1NM),"d1_name","io/output/d1",ConfigType::CharArray,(char*)digitalCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[1].name)};
    // CFGDOC: {"label":"Port physique D1","help":"Identifiant du port physique pilote par la sortie D1. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d1BindingVar_{NVS_KEY(NvsKeys::Io::IO_D1BP),"binding_port","io/output/d1",ConfigType::UInt16,&digitalCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D1AH),"d1_active_high","io/output/d1",ConfigType::Bool,&digitalCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D1IN),"d1_initial_on","io/output/d1",ConfigType::Bool,&digitalCfg_[1].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D1MO),"d1_momentary","io/output/d1",ConfigType::Bool,&digitalCfg_[1].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d1PulseVar_{NVS_KEY(NvsKeys::Io::IO_D1PM),"d1_pulse_ms","io/output/d1",ConfigType::Int32,&digitalCfg_[1].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d2NameVar_{NVS_KEY(NvsKeys::Io::IO_D2NM),"d2_name","io/output/d2",ConfigType::CharArray,(char*)digitalCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[2].name)};
    // CFGDOC: {"label":"Port physique D2","help":"Identifiant du port physique pilote par la sortie D2. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d2BindingVar_{NVS_KEY(NvsKeys::Io::IO_D2BP),"binding_port","io/output/d2",ConfigType::UInt16,&digitalCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D2AH),"d2_active_high","io/output/d2",ConfigType::Bool,&digitalCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D2IN),"d2_initial_on","io/output/d2",ConfigType::Bool,&digitalCfg_[2].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D2MO),"d2_momentary","io/output/d2",ConfigType::Bool,&digitalCfg_[2].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d2PulseVar_{NVS_KEY(NvsKeys::Io::IO_D2PM),"d2_pulse_ms","io/output/d2",ConfigType::Int32,&digitalCfg_[2].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d3NameVar_{NVS_KEY(NvsKeys::Io::IO_D3NM),"d3_name","io/output/d3",ConfigType::CharArray,(char*)digitalCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[3].name)};
    // CFGDOC: {"label":"Port physique D3","help":"Identifiant du port physique pilote par la sortie D3. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d3BindingVar_{NVS_KEY(NvsKeys::Io::IO_D3BP),"binding_port","io/output/d3",ConfigType::UInt16,&digitalCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D3AH),"d3_active_high","io/output/d3",ConfigType::Bool,&digitalCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D3IN),"d3_initial_on","io/output/d3",ConfigType::Bool,&digitalCfg_[3].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D3MO),"d3_momentary","io/output/d3",ConfigType::Bool,&digitalCfg_[3].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d3PulseVar_{NVS_KEY(NvsKeys::Io::IO_D3PM),"d3_pulse_ms","io/output/d3",ConfigType::Int32,&digitalCfg_[3].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d4NameVar_{NVS_KEY(NvsKeys::Io::IO_D4NM),"d4_name","io/output/d4",ConfigType::CharArray,(char*)digitalCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[4].name)};
    // CFGDOC: {"label":"Port physique D4","help":"Identifiant du port physique pilote par la sortie D4. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d4BindingVar_{NVS_KEY(NvsKeys::Io::IO_D4BP),"binding_port","io/output/d4",ConfigType::UInt16,&digitalCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D4AH),"d4_active_high","io/output/d4",ConfigType::Bool,&digitalCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D4IN),"d4_initial_on","io/output/d4",ConfigType::Bool,&digitalCfg_[4].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D4MO),"d4_momentary","io/output/d4",ConfigType::Bool,&digitalCfg_[4].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d4PulseVar_{NVS_KEY(NvsKeys::Io::IO_D4PM),"d4_pulse_ms","io/output/d4",ConfigType::Int32,&digitalCfg_[4].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d5NameVar_{NVS_KEY(NvsKeys::Io::IO_D5NM),"d5_name","io/output/d5",ConfigType::CharArray,(char*)digitalCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[5].name)};
    // CFGDOC: {"label":"Port physique D5","help":"Identifiant du port physique pilote par la sortie D5. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d5BindingVar_{NVS_KEY(NvsKeys::Io::IO_D5BP),"binding_port","io/output/d5",ConfigType::UInt16,&digitalCfg_[5].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D5AH),"d5_active_high","io/output/d5",ConfigType::Bool,&digitalCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D5IN),"d5_initial_on","io/output/d5",ConfigType::Bool,&digitalCfg_[5].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D5MO),"d5_momentary","io/output/d5",ConfigType::Bool,&digitalCfg_[5].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d5PulseVar_{NVS_KEY(NvsKeys::Io::IO_D5PM),"d5_pulse_ms","io/output/d5",ConfigType::Int32,&digitalCfg_[5].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d6NameVar_{NVS_KEY(NvsKeys::Io::IO_D6NM),"d6_name","io/output/d6",ConfigType::CharArray,(char*)digitalCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[6].name)};
    // CFGDOC: {"label":"Port physique D6","help":"Identifiant du port physique pilote par la sortie D6. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d6BindingVar_{NVS_KEY(NvsKeys::Io::IO_D6BP),"binding_port","io/output/d6",ConfigType::UInt16,&digitalCfg_[6].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D6AH),"d6_active_high","io/output/d6",ConfigType::Bool,&digitalCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D6IN),"d6_initial_on","io/output/d6",ConfigType::Bool,&digitalCfg_[6].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D6MO),"d6_momentary","io/output/d6",ConfigType::Bool,&digitalCfg_[6].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d6PulseVar_{NVS_KEY(NvsKeys::Io::IO_D6PM),"d6_pulse_ms","io/output/d6",ConfigType::Int32,&digitalCfg_[6].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d7NameVar_{NVS_KEY(NvsKeys::Io::IO_D7NM),"d7_name","io/output/d7",ConfigType::CharArray,(char*)digitalCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[7].name)};
    // CFGDOC: {"label":"Port physique D7","help":"Identifiant du port physique pilote par la sortie D7. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut."}
    ConfigVariable<PhysicalPortId,0> d7BindingVar_{NVS_KEY(NvsKeys::Io::IO_D7BP),"binding_port","io/output/d7",ConfigType::UInt16,&digitalCfg_[7].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D7AH),"d7_active_high","io/output/d7",ConfigType::Bool,&digitalCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D7IN),"d7_initial_on","io/output/d7",ConfigType::Bool,&digitalCfg_[7].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D7MO),"d7_momentary","io/output/d7",ConfigType::Bool,&digitalCfg_[7].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d7PulseVar_{NVS_KEY(NvsKeys::Io::IO_D7PM),"d7_pulse_ms","io/output/d7",ConfigType::Int32,&digitalCfg_[7].pulseMs,ConfigPersistence::Persistent,0};
};
