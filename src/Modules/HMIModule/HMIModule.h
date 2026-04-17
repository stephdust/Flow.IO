#pragma once
/**
 * @file HMIModule.h
 * @brief UI orchestration module (menu model + HMI driver).
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ServiceBinding.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"
#include "Modules/HMIModule/Drivers/TfaVeniceRf433Sink.h"

class HMIModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::Hmi; }
    const char* taskName() const override { return "HMI"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 4096; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint32_t startDelayMs() const override { return 5000U; }

    uint8_t dependencyCount() const override { return 9; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Io;
        if (i == 5) return ModuleId::Alarm;
        if (i == 6) return ModuleId::Command;
        if (i == 7) return ModuleId::Time;
        if (i == 8) return ModuleId::Wifi;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    struct ConfigData {
        bool ledsEnabled = true;
        bool nextionEnabled = true;
        bool veniceEnabled = false;
        int32_t veniceTxGpio = 14;
    } cfgData_{};

    // CFGDOC: {"label":"Pilotage LEDs facade", "help":"Autorise le HMIModule a ecrire le masque logique des LEDs via StatusLedsService."}
    ConfigVariable<bool,0> ledsEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::LedsEnabled), "enabled", "hmi/leds",
        ConfigType::Bool, &cfgData_.ledsEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Nextion actif", "help":"Autorise le HMIModule a envoyer le rendu et les commandes vers l'ecran Nextion local."}
    ConfigVariable<bool,0> nextionEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::NextionEnabled), "enabled", "hmi/nextion",
        ConfigType::Bool, &cfgData_.nextionEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Venice RF433 actif", "help":"Active l'emission periodique de la temperature d'eau vers un afficheur TFA Venice compatible."}
    ConfigVariable<bool,0> veniceEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceEnabled), "enabled", "hmi/venice",
        ConfigType::Bool, &cfgData_.veniceEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO emission Venice", "help":"GPIO utilise pour l'emetteur RF433 du driver Venice."}
    ConfigVariable<int32_t,0> veniceTxGpioVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceTxGpio), "tx_gpio", "hmi/venice",
        ConfigType::Int32, &cfgData_.veniceTxGpio, ConfigPersistence::Persistent, 0
    };
    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const StatusLedsService* statusLedsSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    ConfigMenuModel menu_;
    NextionDriver nextion_;
    TfaVeniceRf433Sink venice_;
    IHmiDriver* driver_ = nullptr;

    bool driverReady_ = false;
    bool viewDirty_ = true;
    bool configMenuReady_ = false;
    bool configMenuActive_ = false;
    uint32_t lastRenderMs_ = 0;
    uint32_t lastConfigValueRefreshMs_ = 0;
    uint8_t ledPage_ = 1;
    uint8_t ledMaskLast_ = 0;
    bool ledMaskValid_ = false;
    bool wifiBlinkOn_ = false;
    uint32_t homePublishMask_ = 0U;
    portMUX_TYPE homePublishMux_ = portMUX_INITIALIZER_UNLOCKED;
    IoId phIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPh].ioId;
    IoId orpIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotOrp].ioId;
    IoId airTempIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotAirTemp].ioId;
    IoId poolLevelIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPoolLevel].ioId;
    IoId waterTempIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterTemp].ioId;
    uint8_t filtrationDeviceSlot_ = PoolBinding::kDeviceSlotFiltrationPump;
    uint8_t phPumpDeviceSlot_ = PoolBinding::kDeviceSlotPhPump;
    uint8_t orpPumpDeviceSlot_ = PoolBinding::kDeviceSlotChlorinePump;
    uint8_t robotDeviceSlot_ = PoolBinding::kDeviceSlotRobot;
    uint8_t lightsDeviceSlot_ = PoolBinding::kDeviceSlotLights;
    uint8_t heaterDeviceSlot_ = PoolBinding::kDeviceSlotWaterHeater;
    uint8_t fillingDeviceSlot_ = PoolBinding::kDeviceSlotFillPump;
    uint8_t phRuntimeIndex_ = 0xFFU;
    uint8_t orpRuntimeIndex_ = 0xFFU;
    uint8_t waterTempRuntimeIndex_ = 0xFFU;
    uint8_t airTempRuntimeIndex_ = 0xFFU;
    uint8_t poolLevelRuntimeIndex_ = 0xFFU;
    uint32_t lastLedApplyTryMs_ = 0;
    uint32_t lastLedPageToggleMs_ = 0;
    uint32_t lastWifiBlinkToggleMs_ = 0;
    uint32_t lastClockCheckMs_ = 0;
    uint32_t lastClockMinuteStamp_ = 0xFFFFFFFFUL;
    uint32_t lastClockDayStamp_ = 0xFFFFFFFFUL;
    uint32_t lastRtcFallbackAttemptMs_ = 0;
    uint32_t lastRtcPushAttemptMs_ = 0;
    uint32_t lastRtcPushDayStamp_ = 0xFFFFFFFFUL;
    bool rtcFallbackCompleted_ = false;
    char homeErrorMessage_[96]{};

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void handleDriverEvent_(const HmiEvent& e);
    bool requestRefresh_();
    bool openConfigHome_();
    bool openConfigModule_(const char* module);
    bool setLedPage_(uint8_t page);
    uint8_t getLedPage_() const;
    bool refreshCurrentModule_();
    bool render_();
    bool refreshConfigMenuValues_();
    bool buildMenuJson_(char* out, size_t outLen);
    bool ensureConfigMenuReady_();
    void refreshHomeBindings_();
    bool resolveIoRuntimeIndex_(IoId ioId, uint8_t& outIndex) const;
    bool readPoolLogicModeFlags_(bool& autoMode, bool& winterMode, bool& phAutoMode, bool& orpAutoMode) const;
    bool readPidSetpoints_(float& phSetpoint, float& orpSetpoint) const;
    bool readPoolDeviceActualOn_(uint8_t slot, bool& on) const;
    bool isAlarmActive_(AlarmId id) const;
    bool isWaterLevelLow_() const;
    uint32_t buildHomeStateBits_() const;
    uint32_t buildHomeAlarmBits_() const;
    bool publishHomeText_(HmiHomeTextField field);
    bool publishHomeGaugePercent_(HmiHomeGaugeField field);
    bool publishHomeStateBits_();
    bool publishHomeAlarmBits_();
    void serviceRtcBridge_(uint32_t nowMs);
    bool readNextionRtcAndSetTime_();
    bool pushEspTimeToNextionRtc_();
    void queueClockPublishIfDue_(uint32_t nowMs);
    void queueHomePublish_(uint32_t mask);
    void flushHomePublish_();
    bool executeHmiCommand_(HmiCommandId command, uint8_t value);
    bool executeCommandBool_(const char* cmdName, bool value);
    bool executePoolDeviceWrite_(uint8_t slot, bool value);
    bool executePoolLogicModePatch_(const char* key, bool value);
    void setHomeErrorMessage_(const char* message, bool forceStateRefresh);
    void reportCommandError_(const char* operation, const char* reply);
    void applyOutputConfig_();
    void applyLedMask_(bool force = false);

    HmiService hmiSvc_{
        ServiceBinding::bind<&HMIModule::requestRefresh_>,
        ServiceBinding::bind<&HMIModule::openConfigHome_>,
        ServiceBinding::bind<&HMIModule::openConfigModule_>,
        ServiceBinding::bind<&HMIModule::buildMenuJson_>,
        ServiceBinding::bind<&HMIModule::setLedPage_>,
        ServiceBinding::bind_or<&HMIModule::getLedPage_, (uint8_t)1U>,
        this
    };
};
