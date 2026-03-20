#pragma once
/**
 * @file HMIModule.h
 * @brief UI orchestration module (menu model + HMI driver).
 */

#include "Core/Module.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"

class HMIModule : public Module {
public:
    const char* moduleId() const override { return "hmi"; }
    const char* taskName() const override { return "HMI"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 4096; }

    uint8_t dependencyCount() const override { return 6; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "config";
        if (i == 2) return "eventbus";
        if (i == 3) return "datastore";
        if (i == 4) return "io";
        if (i == 5) return "alarms";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const StatusLedsService* statusLedsSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    ConfigMenuModel menu_;
    NextionDriver nextion_;
    IHmiDriver* driver_ = nullptr;

    bool driverReady_ = false;
    bool viewDirty_ = true;
    uint32_t lastRenderMs_ = 0;
    uint8_t ledPage_ = 1;
    uint8_t ledMaskLast_ = 0;
    bool ledMaskValid_ = false;
    bool mqttReady_ = false;
    bool autoRegEnabled_ = false;
    bool winterMode_ = false;
    bool phPidEnabled_ = false;
    bool chlorinePidEnabled_ = false;
    bool phTankLowAlarm_ = false;
    bool chlorineTankLowAlarm_ = false;
    bool phPumpRuntimeAlarm_ = false;
    bool chlorinePumpRuntimeAlarm_ = false;
    bool psiAlarm_ = false;
    char poollogicCfgJson_[768]{};
    uint32_t lastLedApplyTryMs_ = 0;
    uint32_t lastLedPageToggleMs_ = 0;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void handleDriverEvent_(const HmiEvent& e);
    bool refreshCurrentModule_();
    bool render_();
    bool buildMenuJson_(char* out, size_t outLen) const;

    static bool svcRequestRefresh_(void* ctx);
    static bool svcOpenConfigHome_(void* ctx);
    static bool svcOpenConfigModule_(void* ctx, const char* module);
    static bool svcBuildConfigMenuJson_(void* ctx, char* out, size_t outLen);
    static bool svcSetLedPage_(void* ctx, uint8_t page);
    static uint8_t svcGetLedPage_(void* ctx);
    void refreshPoolLogicFlags_();
    void refreshRuntimeFlags_();
    void refreshAlarmFlags_();
    void updatePumpRuntimeAlarmFromSlot_(uint8_t slot);
    void applyLedMask_(bool force = false);
};
