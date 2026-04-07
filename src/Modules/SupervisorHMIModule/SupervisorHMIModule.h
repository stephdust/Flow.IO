#pragma once
/**
 * @file SupervisorHMIModule.h
 * @brief Local Supervisor HMI module (ST7789 + PIR + factory reset button).
 */

#include "Core/EventBus/EventBus.h"
#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

struct BoardSpec;
struct SupervisorRuntimeOptions;

class SupervisorHMIModule : public Module {
public:
    SupervisorHMIModule(const BoardSpec& board, const SupervisorRuntimeOptions& runtime);

    ModuleId moduleId() const override { return ModuleId::SupervisorHmi; }
    const char* taskName() const override { return "hmi.sup"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 6144; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 8; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::EventBus;
        if (i == 4) return ModuleId::DataStore;
        if (i == 5) return ModuleId::Wifi;
        if (i == 6) return ModuleId::WifiProvisioning;
        if (i == 7) return ModuleId::FirmwareUpdate;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static St7789SupervisorDriverConfig makeDriverConfig_(const BoardSpec& board);
    static void copyText_(char* out, size_t outLen, const char* in);
    static void onEventStatic_(const Event& e, void* user);
    uint32_t buildRenderKey_() const;
    uint32_t currentClockMinute_() const;
    uint32_t currentPageCycle_() const;

    void onEvent_(const Event& e);
    void pollWifiAndNetwork_();
    void pollFirmwareStatus_();
    void refreshFlowStatusFromDataStore_();
    void updateBacklight_();
    void updateFactoryResetButton_();
    void scheduleFactoryReset_();
    void executePendingFactoryReset_();
    void rebuildBanner_();
    void setDefaultBanner_();

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const EventBusService* eventBusSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    St7789SupervisorDriverConfig driverCfg_{};
    St7789SupervisorDriver driver_;
    SupervisorHmiViewModel view_{};
    bool driverReady_ = false;
    volatile bool flowRuntimeDirty_ = true;

    int8_t pirPin_ = -1;
    int8_t factoryResetPin_ = -1;
    uint32_t pirTimeoutMs_ = 60000U;
    uint32_t pirDebounceMs_ = 120U;
    bool pirActiveHigh_ = true;
    uint32_t factoryResetHoldMs_ = 5000U;
    uint32_t factoryResetDebounceMs_ = 40U;

    bool fwBusyOrPending_ = false;
    bool factoryResetPending_ = false;
    uint32_t factoryResetExecuteAtMs_ = 0;

    uint32_t lastFwPollMs_ = 0;
    uint32_t lastRenderMs_ = 0;
    uint32_t splashHoldUntilMs_ = 0;
    uint32_t backlightForceOnUntilMs_ = 0;
    uint32_t lastRenderKey_ = 0;
    uint32_t lastRenderedMinute_ = 0;
    uint32_t lastRenderedPageCycle_ = 0;
    bool hasLastRenderKey_ = false;
    bool lastBacklightOn_ = false;

    bool buttonPressed_ = false;
    bool buttonTriggered_ = false;
    bool buttonArmed_ = false;
    bool buttonRawPressed_ = false;
    bool buttonStablePressed_ = false;
    uint32_t buttonPressedAtMs_ = 0;
    uint32_t buttonHighSinceMs_ = 0;
    uint32_t buttonDebounceChangedAtMs_ = 0;

    bool pirRawState_ = false;
    bool pirStableState_ = false;
    uint32_t pirDebounceChangedAtMs_ = 0;
    uint32_t lastMotionMs_ = 0;
};
