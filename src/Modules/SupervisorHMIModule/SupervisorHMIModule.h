#pragma once
/**
 * @file SupervisorHMIModule.h
 * @brief Local Supervisor HMI module (ST7789 + PIR + WiFi reset button).
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

class SupervisorHMIModule : public Module {
public:
    SupervisorHMIModule();

    const char* moduleId() const override { return "hmi.supervisor"; }
    const char* taskName() const override { return "hmi.sup"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 6144; }

    uint8_t dependencyCount() const override { return 6; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "config";
        if (i == 2) return "wifi";
        if (i == 3) return "wifiprov";
        if (i == 4) return "fwupdate";
        if (i == 5) return "i2ccfg.client";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static St7789SupervisorDriverConfig makeDriverConfig_();
    static void copyText_(char* out, size_t outLen, const char* in);
    uint32_t buildRenderKey_() const;
    uint32_t currentClockMinute_() const;

    void pollWifiAndNetwork_();
    void pollFirmwareStatus_();
    void pollFlowStatus_();
    void updateBacklight_();
    void updateWifiResetButton_();
    void triggerWifiReset_();
    void rebuildBanner_();

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;

    St7789SupervisorDriver driver_;
    SupervisorHmiViewModel view_{};
    bool driverReady_ = false;

    bool fwBusyOrPending_ = false;
    bool wifiResetPending_ = false;
    uint32_t restartAtMs_ = 0;

    uint32_t lastFwPollMs_ = 0;
    uint32_t lastFlowPollMs_ = 0;
    uint32_t lastRenderMs_ = 0;
    uint32_t splashHoldUntilMs_ = 0;
    uint32_t lastRenderKey_ = 0;
    uint32_t lastRenderedMinute_ = 0;
    bool hasLastRenderKey_ = false;
    bool lastBacklightOn_ = false;
    char flowStatusScratchBuf_[448] = {0};

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
