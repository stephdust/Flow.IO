#pragma once

#include "Core/Module.h"
#include "Core/ConfigTypes.h"
#include "Modules/Micronova/MicronovaBusModule/MicronovaBusModule.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaRegisterMap.h"

#include <stdint.h>

class MicronovaBoilerModule : public Module {
public:
    MicronovaBoilerModule();

    ModuleId moduleId() const override { return ModuleId::MicronovaBoiler; }
    const char* taskName() const override { return "micronova.boiler"; }
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint16_t taskStackSize() const override { return 4096; }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::DataStore;
        if (i == 3) return ModuleId::MicronovaBus;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::MicronovaBoilerStartDelayMs; }

    void setBus(MicronovaBusModule* bus) { bus_ = bus; }

    bool begin();
    void tick(uint32_t nowMs);

    bool setPower(bool on);
    bool setPowerLevel(uint8_t level);
    bool setFanSpeed(uint8_t level);
    bool setTargetTemperature(uint8_t temperature);
    bool sendPowerPlus();
    bool sendPowerMinus();
    bool refreshNow();

private:
    struct RegisterConfig {
        int32_t readCode = 0;
        int32_t writeCode = 0;
        int32_t address = 0;
        float scale = 1.0f;
        float offset = 0.0f;
        bool writable = false;
        bool enabled = true;
    };

    struct CommandConfig {
        int32_t writeCode = 0;
        int32_t address = 0;
        int32_t value = 0;
        int32_t repeatCount = 1;
        int32_t repeatDelayMs = MicronovaProtocol::DefaultRepeatDelayMs;
    };

    void handleRawValue_(const MicronovaRawValue& value);
    void publishRuntimeValue_(MicronovaRegisterId id, float converted, int16_t raw, uint32_t nowMs);
    void handleCommandEvent_(const Event& e);
    static void onEventStatic_(const Event& e, void* user);
    void syncOnline_(uint32_t nowMs);
    void queuePollingSweep_(uint32_t nowMs);
    void requestReadAllSweep_();
    void startReadAllSweep_(uint32_t nowMs);
    void tickReadAllSweep_(uint32_t nowMs);
    void finishReadAllSweep_(bool aborted);
    void onReadAllSweepReply_(const MicronovaRawValue& value, uint32_t nowMs);
    bool updateDisplayLinesFromRaw_(const MicronovaRawValue& value, uint32_t nowMs);
    void queueDisplayLineReads_();
    bool queueRegisterRead_(MicronovaRegisterId id);
    const RegisterConfig& reg_(MicronovaRegisterId id) const;
    RegisterConfig& reg_(MicronovaRegisterId id);
    bool writeRegister_(MicronovaRegisterId id, uint8_t value);
    bool writeCommand_(const CommandConfig& command, MicronovaWriteTxMode txMode = MicronovaWriteTxMode::Default);
    void recordLastCommand_(const char* command);
    uint8_t clampLevel_(uint8_t level) const;

    MicronovaBusModule* bus_ = nullptr;
    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;
    bool begun_ = false;
    bool lastOnline_ = false;
    uint32_t nextPollMs_ = 0;
    uint16_t fastCyclesRemaining_ = 0;
    bool readAllSweepRequested_ = false;
    bool readAllSweepActive_ = false;
    bool readAllSweepAwaitingReply_ = false;
    uint8_t readAllSweepAddress_ = 0;
    uint16_t readAllSweepIndex_ = 0;
    uint32_t readAllSweepSentMs_ = 0;
    uint32_t readAllSweepNextStepMs_ = 0;
    uint16_t readAllSweepValidCount_ = 0;
    uint16_t readAllSweepInvalidCount_ = 0;
    uint16_t readAllSweepTimeoutCount_ = 0;
    uint8_t displayLineDirtyMask_ = 0U;

    RegisterConfig regs_[kMicronovaRegisterCount]{};
    CommandConfig powerOn_{};
    CommandConfig powerOff_{};
    CommandConfig powerPlus_{};
    CommandConfig powerMinus_{};

    int32_t normalIntervalMs_ = 60000;
    int32_t fastIntervalMs_ = 20000;
    int32_t fastCyclesCfg_ = 30;

    ConfigVariable<int32_t,0> normalIntervalVar_;
    ConfigVariable<int32_t,0> fastIntervalVar_;
    ConfigVariable<int32_t,0> fastCyclesVar_;

    ConfigVariable<int32_t,0> regReadVars_[kMicronovaRegisterCount];
    ConfigVariable<int32_t,0> regWriteVars_[kMicronovaRegisterCount];
    ConfigVariable<int32_t,0> regAddressVars_[kMicronovaRegisterCount];
    ConfigVariable<float,0> regScaleVars_[kMicronovaRegisterCount];
    ConfigVariable<bool,0> regEnabledVars_[kMicronovaRegisterCount];

    ConfigVariable<int32_t,0> powerOnWriteVar_;
    ConfigVariable<int32_t,0> powerOnAddressVar_;
    ConfigVariable<int32_t,0> powerOnValueVar_;
    ConfigVariable<int32_t,0> powerOnRepeatCountVar_;
    ConfigVariable<int32_t,0> powerOnRepeatDelayVar_;
    ConfigVariable<int32_t,0> powerOffWriteVar_;
    ConfigVariable<int32_t,0> powerOffAddressVar_;
    ConfigVariable<int32_t,0> powerOffValueVar_;
    ConfigVariable<int32_t,0> powerOffRepeatCountVar_;
    ConfigVariable<int32_t,0> powerOffRepeatDelayVar_;
    ConfigVariable<int32_t,0> powerPlusWriteVar_;
    ConfigVariable<int32_t,0> powerPlusAddressVar_;
    ConfigVariable<int32_t,0> powerPlusValueVar_;
    ConfigVariable<int32_t,0> powerPlusRepeatCountVar_;
    ConfigVariable<int32_t,0> powerPlusRepeatDelayVar_;
    ConfigVariable<int32_t,0> powerMinusWriteVar_;
    ConfigVariable<int32_t,0> powerMinusAddressVar_;
    ConfigVariable<int32_t,0> powerMinusValueVar_;
    ConfigVariable<int32_t,0> powerMinusRepeatCountVar_;
    ConfigVariable<int32_t,0> powerMinusRepeatDelayVar_;
};
