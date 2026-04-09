/**
 * @file SupervisorHMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"

#include "Board/BoardSpec.h"
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientRuntime.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

#include "App/FirmwareProfile.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

namespace {
static constexpr uint32_t kFwPollMs = 500U;
static constexpr uint32_t kStartupSplashHoldMs = 5000U;
static constexpr uint32_t kStartupBacklightForceOnMs = 60000U;
static constexpr uint32_t kButtonArmHighStableMs = 500U;
static constexpr uint32_t kFactoryResetMessageDelayMs = 900U;
static constexpr uint32_t kPageRotateMs = 10000U;
static constexpr uint32_t kWifiRssiRefreshMs = 3000U;

const SupervisorBoardSpec& supervisorBoardSpec_(const BoardSpec& board)
{
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            33,
            14,
            15,
            4,
            5,
            35,
            19,
            18,
            true,
            false,
            40000000U,
            80
        },
        {
            36,
            120,
            true,
            23,
            40
        },
        {
            25,
            26,
            13,
            115200U
        }
    };
    const SupervisorBoardSpec* cfg = boardSupervisorConfig(board);
    return cfg ? *cfg : kFallback;
}

} // namespace

SupervisorHMIModule::SupervisorHMIModule(const BoardSpec& board, const SupervisorRuntimeOptions& runtime)
    : driverCfg_(makeDriverConfig_(board)),
      driver_(driverCfg_)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    pirPin_ = boardCfg.inputs.pirPin;
    factoryResetPin_ = boardCfg.inputs.factoryResetPin;
    pirTimeoutMs_ = runtime.pirTimeoutMs;
    pirDebounceMs_ = boardCfg.inputs.pirDebounceMs;
    pirActiveHigh_ = boardCfg.inputs.pirActiveHigh;
    factoryResetHoldMs_ = runtime.factoryResetHoldMs;
    factoryResetDebounceMs_ = boardCfg.inputs.factoryResetDebounceMs;
}

St7789SupervisorDriverConfig SupervisorHMIModule::makeDriverConfig_(const BoardSpec& board)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    St7789SupervisorDriverConfig cfg{};
    cfg.resX = boardCfg.display.resX;
    cfg.resY = boardCfg.display.resY;
    cfg.rotation = boardCfg.display.rotation;
    cfg.colStart = boardCfg.display.colStart;
    cfg.rowStart = boardCfg.display.rowStart;
    cfg.backlightPin = boardCfg.display.backlightPin;
    cfg.csPin = boardCfg.display.csPin;
    cfg.dcPin = boardCfg.display.dcPin;
    cfg.rstPin = boardCfg.display.rstPin;
    cfg.misoPin = boardCfg.display.misoPin;
    cfg.mosiPin = boardCfg.display.mosiPin;
    cfg.sclkPin = boardCfg.display.sclkPin;
    cfg.swapColorBytes = boardCfg.display.swapColorBytes;
    cfg.invertColors = boardCfg.display.invertColors;
    cfg.spiHz = boardCfg.display.spiHz;
    cfg.minRenderGapMs = boardCfg.display.minRenderGapMs;
    return cfg;
}

void SupervisorHMIModule::copyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";
    snprintf(out, outLen, "%s", in);
}

uint32_t SupervisorHMIModule::buildRenderKey_() const
{
    uint32_t h = 2166136261U;
    auto mix = [&h](const void* ptr, size_t len) {
        if (!ptr || len == 0) return;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; i++) {
            h ^= (uint32_t)b[i];
            h *= 16777619U;
        }
    };

    mix(&view_.wifiConnected, sizeof(view_.wifiConnected));
    mix(&view_.wifiState, sizeof(view_.wifiState));
    mix(&view_.accessMode, sizeof(view_.accessMode));
    mix(&view_.hasRssi, sizeof(view_.hasRssi));
    mix(&view_.rssiDbm, sizeof(view_.rssiDbm));
    mix(&view_.pageIndex, sizeof(view_.pageIndex));
    mix(view_.ip, strnlen(view_.ip, sizeof(view_.ip)) + 1U);
    mix(&view_.flowLinkOk, sizeof(view_.flowLinkOk));
    mix(&view_.flowMqttReady, sizeof(view_.flowMqttReady));
    mix(&view_.flowHasPoolModes, sizeof(view_.flowHasPoolModes));
    mix(&view_.flowFiltrationAuto, sizeof(view_.flowFiltrationAuto));
    mix(&view_.flowWinterMode, sizeof(view_.flowWinterMode));
    mix(&view_.flowPhAutoMode, sizeof(view_.flowPhAutoMode));
    mix(&view_.flowOrpAutoMode, sizeof(view_.flowOrpAutoMode));
    mix(&view_.flowFiltrationOn, sizeof(view_.flowFiltrationOn));
    mix(&view_.flowPhPumpOn, sizeof(view_.flowPhPumpOn));
    mix(&view_.flowChlorinePumpOn, sizeof(view_.flowChlorinePumpOn));
    mix(&view_.flowHasPh, sizeof(view_.flowHasPh));
    mix(&view_.flowPhValue, sizeof(view_.flowPhValue));
    mix(&view_.flowHasOrp, sizeof(view_.flowHasOrp));
    mix(&view_.flowOrpValue, sizeof(view_.flowOrpValue));
    mix(&view_.flowHasWaterTemp, sizeof(view_.flowHasWaterTemp));
    mix(&view_.flowWaterTemp, sizeof(view_.flowWaterTemp));
    mix(&view_.flowHasAirTemp, sizeof(view_.flowHasAirTemp));
    mix(&view_.flowAirTemp, sizeof(view_.flowAirTemp));
    mix(&view_.flowHasWaterCounter, sizeof(view_.flowHasWaterCounter));
    mix(&view_.flowWaterCounter, sizeof(view_.flowWaterCounter));
    mix(&view_.flowHasPsi, sizeof(view_.flowHasPsi));
    mix(&view_.flowPsi, sizeof(view_.flowPsi));
    mix(&view_.flowHasBmp280Temp, sizeof(view_.flowHasBmp280Temp));
    mix(&view_.flowBmp280Temp, sizeof(view_.flowBmp280Temp));
    mix(&view_.flowHasBme680Temp, sizeof(view_.flowHasBme680Temp));
    mix(&view_.flowBme680Temp, sizeof(view_.flowBme680Temp));
    mix(&view_.flowAlarmActiveMask, sizeof(view_.flowAlarmActiveMask));
    mix(&view_.flowAlarmResettableMask, sizeof(view_.flowAlarmResettableMask));
    mix(&view_.flowAlarmConditionMask, sizeof(view_.flowAlarmConditionMask));
    for (uint8_t i = 0U; i < kSupervisorDashboardSlotCount; ++i) {
        const SupervisorDashboardSlotViewModel& slot = view_.flowDashboardSlots[i];
        mix(&slot.enabled, sizeof(slot.enabled));
        mix(&slot.runtimeUiId, sizeof(slot.runtimeUiId));
        mix(slot.label, strnlen(slot.label, sizeof(slot.label)) + 1U);
        mix(&slot.bgColor565, sizeof(slot.bgColor565));
        mix(&slot.available, sizeof(slot.available));
        mix(&slot.wireType, sizeof(slot.wireType));
        mix(&slot.boolValue, sizeof(slot.boolValue));
        mix(&slot.i32Value, sizeof(slot.i32Value));
        mix(&slot.u32Value, sizeof(slot.u32Value));
        mix(&slot.f32Value, sizeof(slot.f32Value));
        mix(&slot.enumValue, sizeof(slot.enumValue));
    }
    mix(&view_.flowAlarmActiveCount, sizeof(view_.flowAlarmActiveCount));
    mix(&view_.flowAlarmCodeCount, sizeof(view_.flowAlarmCodeCount));
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); i++) {
        mix(view_.flowAlarmCodes[i], strnlen(view_.flowAlarmCodes[i], sizeof(view_.flowAlarmCodes[i])) + 1U);
    }
    mix(&view_.flowAlarmActCount, sizeof(view_.flowAlarmActCount));
    mix(&view_.flowAlarmResettableCount, sizeof(view_.flowAlarmResettableCount));
    mix(&view_.flowAlarmClrCount, sizeof(view_.flowAlarmClrCount));
    mix(view_.flowAlarmStates, sizeof(view_.flowAlarmStates));
    mix(&view_.factoryResetPending, sizeof(view_.factoryResetPending));
    mix(view_.banner, strnlen(view_.banner, sizeof(view_.banner)) + 1U);

    return h;
}

uint32_t SupervisorHMIModule::currentClockMinute_() const
{
    const time_t now = time(nullptr);
    if (now <= 1600000000) return 0U;
    return (uint32_t)(now / 60);
}

uint32_t SupervisorHMIModule::currentPageCycle_() const
{
    return (uint32_t)(millis() / kPageRotateMs);
}

void SupervisorHMIModule::onEventStatic_(const Event& e, void* user)
{
    SupervisorHMIModule* self = static_cast<SupervisorHMIModule*>(user);
    if (self) self->onEvent_(e);
}

void SupervisorHMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    eventBusSvc_ = services.get<EventBusService>(ServiceId::EventBus);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    eventBus_ = eventBusSvc_ ? eventBusSvc_->bus : nullptr;
    (void)logHub_;

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &SupervisorHMIModule::onEventStatic_, this);
    }

    if (pirPin_ >= 0) {
        int pirMode = INPUT;
#if defined(ESP32)
        // ESP32 GPIO 34..39 are input-only and don't support internal pull resistors.
        if (pirPin_ <= 33) {
            pirMode = pirActiveHigh_ ? INPUT_PULLDOWN : INPUT_PULLUP;
        }
#endif
        pinMode(pirPin_, pirMode);
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawPir = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        pirRawState_ = rawPir;
        pirStableState_ = rawPir;
        pirDebounceChangedAtMs_ = millis();
    }
    if (factoryResetPin_ >= 0) {
        pinMode(factoryResetPin_, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(factoryResetPin_) == LOW);
        buttonRawPressed_ = rawPressed;
        buttonStablePressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = millis();
    }

    lastMotionMs_ = millis();
    copyText_(view_.fwState, sizeof(view_.fwState), "idle");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), "none");
    setDefaultBanner_();
    refreshFlowStatusFromDataStore_();

    LOGI("Supervisor HMI initialized cs=%d dc=%d rst=%d bl=%d miso=%d mosi=%d sclk=%d hz=%d rot=%d col=%d row=%d pir=%d pah=%d wr=%d",
         (int)driverCfg_.csPin,
         (int)driverCfg_.dcPin,
         (int)driverCfg_.rstPin,
         (int)driverCfg_.backlightPin,
         (int)driverCfg_.misoPin,
         (int)driverCfg_.mosiPin,
         (int)driverCfg_.sclkPin,
         (int)driverCfg_.spiHz,
         (int)driverCfg_.rotation,
         (int)driverCfg_.colStart,
         (int)driverCfg_.rowStart,
         (int)pirPin_,
         (int)(pirActiveHigh_ ? 1 : 0),
         (int)factoryResetPin_);
}

void SupervisorHMIModule::onEvent_(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (p->id < DataKeys::FlowRemoteBase || p->id >= DataKeys::FlowRemoteEndExclusive) return;
    flowRuntimeDirty_ = true;
}

void SupervisorHMIModule::pollWifiAndNetwork_()
{
    view_.wifiConnected = false;
    view_.wifiState = WifiState::Idle;
    view_.accessMode = NetworkAccessMode::None;
    view_.netReachable = false;
    view_.hasRssi = false;
    view_.rssiDbm = -127;
    view_.ip[0] = '\0';

    if (wifiSvc_) {
        if (wifiSvc_->state) view_.wifiState = wifiSvc_->state(wifiSvc_->ctx);
        if (wifiSvc_->isConnected) view_.wifiConnected = wifiSvc_->isConnected(wifiSvc_->ctx);
        if (wifiSvc_->getIP) (void)wifiSvc_->getIP(wifiSvc_->ctx, view_.ip, sizeof(view_.ip));
    }

    if (netAccessSvc_) {
        if (netAccessSvc_->mode) view_.accessMode = netAccessSvc_->mode(netAccessSvc_->ctx);
        if (netAccessSvc_->isWebReachable) view_.netReachable = netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
        if (view_.ip[0] == '\0' && netAccessSvc_->getIP) {
            (void)netAccessSvc_->getIP(netAccessSvc_->ctx, view_.ip, sizeof(view_.ip));
        }
    }

    if (view_.wifiConnected && WiFi.status() == WL_CONNECTED) {
        const uint32_t now = millis();
        const int32_t rawRssiDbm = (int32_t)WiFi.RSSI();
        if (!cachedWifiHasRssi_ || (uint32_t)(now - lastWifiRssiRefreshMs_) >= kWifiRssiRefreshMs) {
            cachedWifiRssiDbm_ = rawRssiDbm;
            cachedWifiHasRssi_ = true;
            lastWifiRssiRefreshMs_ = now;
        }
        view_.rssiDbm = cachedWifiRssiDbm_;
        view_.hasRssi = cachedWifiHasRssi_;
    } else {
        cachedWifiHasRssi_ = false;
        cachedWifiRssiDbm_ = -127;
    }
}

void SupervisorHMIModule::pollFirmwareStatus_()
{
    if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) return;

    char json[320] = {0};
    if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, json, sizeof(json))) return;

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    copyText_(view_.fwState, sizeof(view_.fwState), root["state"] | "n/a");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), root["target"] | "n/a");
    view_.fwProgress = (uint8_t)(root["progress"] | 0U);
    copyText_(view_.fwMsg, sizeof(view_.fwMsg), root["msg"] | "");
    view_.fwBusy = root["busy"] | false;
    view_.fwPending = root["pending"] | false;
    fwBusyOrPending_ = view_.fwBusy || view_.fwPending;
}

void SupervisorHMIModule::refreshFlowStatusFromDataStore_()
{
    if (!dsSvc_ || !dsSvc_->store) return;
    const FlowRemoteRuntimeData& flow = flowRemoteRuntime(*dsSvc_->store);
    view_.flowCfgReady = flow.ready;
    view_.flowLinkOk = flow.linkOk;
    copyText_(view_.flowFirmware, sizeof(view_.flowFirmware), flow.firmware);
    view_.flowHasRssi = flow.hasRssi;
    view_.flowRssiDbm = flow.rssiDbm;
    view_.flowHasHeapFrag = flow.hasHeapFrag;
    view_.flowHeapFragPct = flow.heapFragPct;
    view_.flowMqttReady = flow.mqttReady;
    view_.flowMqttRxDrop = flow.mqttRxDrop;
    view_.flowMqttParseFail = flow.mqttParseFail;
    view_.flowI2cReqCount = flow.i2cReqCount;
    view_.flowI2cBadReqCount = flow.i2cBadReqCount;
    view_.flowI2cLastReqAgoMs = flow.i2cLastReqAgoMs;
    view_.flowHasPoolModes = flow.hasPoolModes;
    view_.flowFiltrationAuto = flow.filtrationAuto;
    view_.flowWinterMode = flow.winterMode;
    view_.flowPhAutoMode = flow.phAutoMode;
    view_.flowOrpAutoMode = flow.orpAutoMode;
    view_.flowFiltrationOn = flow.filtrationOn;
    view_.flowPhPumpOn = flow.phPumpOn;
    view_.flowChlorinePumpOn = flow.chlorinePumpOn;
    view_.flowHasPh = flow.hasPh;
    view_.flowPhValue = flow.phValue;
    view_.flowHasOrp = flow.hasOrp;
    view_.flowOrpValue = flow.orpValue;
    view_.flowHasWaterTemp = flow.hasWaterTemp;
    view_.flowWaterTemp = flow.waterTemp;
    view_.flowHasAirTemp = flow.hasAirTemp;
    view_.flowAirTemp = flow.airTemp;
    view_.flowHasWaterCounter = flow.hasWaterCounter;
    view_.flowWaterCounter = flow.waterCounter;
    view_.flowHasPsi = flow.hasPsi;
    view_.flowPsi = flow.psi;
    view_.flowHasBmp280Temp = flow.hasBmp280Temp;
    view_.flowBmp280Temp = flow.bmp280Temp;
    view_.flowHasBme680Temp = flow.hasBme680Temp;
    view_.flowBme680Temp = flow.bme680Temp;
    view_.flowAlarmActiveMask = flow.alarmActiveMask;
    view_.flowAlarmResettableMask = flow.alarmResettableMask;
    view_.flowAlarmConditionMask = flow.alarmConditionMask;
    for (uint8_t i = 0U; i < kSupervisorDashboardSlotCount; ++i) {
        const FlowRemoteDashboardSlotRuntime& src = flow.dashboardSlots[i];
        SupervisorDashboardSlotViewModel& dst = view_.flowDashboardSlots[i];
        dst.enabled = src.enabled;
        dst.runtimeUiId = src.runtimeUiId;
        snprintf(dst.label, sizeof(dst.label), "%s", src.label);
        dst.bgColor565 = src.bgColor565;
        dst.available = src.available;
        dst.wireType = src.wireType;
        dst.boolValue = src.boolValue;
        dst.i32Value = src.i32Value;
        dst.u32Value = src.u32Value;
        dst.f32Value = src.f32Value;
        dst.enumValue = src.enumValue;
    }
    view_.flowAlarmActCount = 0U;
    view_.flowAlarmResettableCount = 0U;
    view_.flowAlarmClrCount = 0U;
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        const uint32_t mask = (1UL << i);
        const bool active = (view_.flowAlarmActiveMask & mask) != 0U;
        const bool resettable = (view_.flowAlarmResettableMask & mask) != 0U;
        if (resettable) {
            view_.flowAlarmStates[i] = SupervisorAlarmState::Resettable;
            ++view_.flowAlarmResettableCount;
        } else if (active) {
            view_.flowAlarmStates[i] = SupervisorAlarmState::Active;
            ++view_.flowAlarmActCount;
        } else {
            view_.flowAlarmStates[i] = SupervisorAlarmState::Clear;
            ++view_.flowAlarmClrCount;
        }
    }
}

void SupervisorHMIModule::scheduleFactoryReset_()
{
    if (factoryResetPending_) return;
    factoryResetPending_ = true;
    factoryResetExecuteAtMs_ = millis() + kFactoryResetMessageDelayMs;
    copyText_(view_.banner, sizeof(view_.banner), "Factory reset starting...");
    LOGW("Factory reset requested from local button");
}

void SupervisorHMIModule::executePendingFactoryReset_()
{
    if (!factoryResetPending_ || factoryResetExecuteAtMs_ == 0U) return;
    if ((int32_t)(millis() - factoryResetExecuteAtMs_) < 0) return;

    factoryResetExecuteAtMs_ = 0U;
    if (!cmdSvc_ || !cmdSvc_->execute) {
        factoryResetPending_ = false;
        copyText_(view_.banner, sizeof(view_.banner), "Factory reset failed: command unavailable");
        LOGE("Factory reset failed: command service unavailable");
        return;
    }

    char reply[160] = {0};
    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.factory_reset", "{}", nullptr, reply, sizeof(reply));
    if (!ok) {
        factoryResetPending_ = false;
        copyText_(view_.banner, sizeof(view_.banner), "Factory reset failed");
        LOGE("Factory reset command failed reply=%s", reply[0] ? reply : "<empty>");
    }
}

void SupervisorHMIModule::updateFactoryResetButton_()
{
    if (factoryResetPin_ < 0 || factoryResetPending_) return;

    const uint32_t now = millis();
    const bool rawPressed = (digitalRead(factoryResetPin_) == LOW);

    if (rawPressed != buttonRawPressed_) {
        buttonRawPressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = now;
    }
    if ((uint32_t)(now - buttonDebounceChangedAtMs_) >= factoryResetDebounceMs_) {
        buttonStablePressed_ = buttonRawPressed_;
    }

    const bool isPressed = buttonStablePressed_;

    // Safety: do not accept press events until we've seen a stable released
    // level after boot. This avoids false long-press triggers on floating lines.
    if (!buttonArmed_) {
        if (isPressed) {
            buttonHighSinceMs_ = 0;
            buttonPressed_ = false;
            buttonTriggered_ = false;
            return;
        }

        if (buttonHighSinceMs_ == 0U) {
            buttonHighSinceMs_ = now;
            return;
        }

        if ((uint32_t)(now - buttonHighSinceMs_) < kButtonArmHighStableMs) {
            return;
        }
        buttonArmed_ = true;
        LOGI("Factory reset button armed");
        return;
    }

    if (!isPressed) {
        buttonPressed_ = false;
        buttonTriggered_ = false;
        return;
    }

    if (!buttonPressed_) {
        buttonPressed_ = true;
        buttonPressedAtMs_ = now;
        const uint32_t holdSeconds = (factoryResetHoldMs_ + 999U) / 1000U;
        snprintf(view_.banner,
                 sizeof(view_.banner),
                 "Keep holding %lus for factory reset",
                 (unsigned long)holdSeconds);
        return;
    }

    if (buttonTriggered_) return;
    if ((uint32_t)(now - buttonPressedAtMs_) < factoryResetHoldMs_) return;

    buttonTriggered_ = true;
    scheduleFactoryReset_();
}

void SupervisorHMIModule::updateBacklight_()
{
    const uint32_t now = millis();
    const bool forceBacklightOn = (int32_t)(now - backlightForceOnUntilMs_) < 0;
    bool motion = false;
    if (pirPin_ >= 0) {
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawMotion = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        if (rawMotion != pirRawState_) {
            pirRawState_ = rawMotion;
            pirDebounceChangedAtMs_ = now;
        }
        if ((uint32_t)(now - pirDebounceChangedAtMs_) >= pirDebounceMs_) {
            pirStableState_ = pirRawState_;
        }
        motion = pirStableState_;
    }
    if (motion || fwBusyOrPending_) {
        lastMotionMs_ = now;
    }

    if (pirPin_ < 0) {
        driver_.setBacklight(true);
        return;
    }

    const bool keepOn = forceBacklightOn || fwBusyOrPending_ || ((uint32_t)(now - lastMotionMs_) <= pirTimeoutMs_);
    driver_.setBacklight(keepOn);
}

void SupervisorHMIModule::rebuildBanner_()
{
    if (factoryResetPending_) return;
    if (!driver_.isBacklightOn()) {
        copyText_(view_.banner, sizeof(view_.banner), "PIR idle: backlight off");
        return;
    }
    if (buttonPressed_ && !buttonTriggered_) {
        const uint32_t holdSeconds = (factoryResetHoldMs_ + 999U) / 1000U;
        snprintf(view_.banner,
                 sizeof(view_.banner),
                 "Keep holding %lus for factory reset",
                 (unsigned long)holdSeconds);
        return;
    }
    setDefaultBanner_();
}

void SupervisorHMIModule::setDefaultBanner_()
{
    if (factoryResetPin_ < 0) {
        copyText_(view_.banner, sizeof(view_.banner), "Local display ready");
        return;
    }
    const uint32_t holdSeconds = (factoryResetHoldMs_ + 999U) / 1000U;
    snprintf(view_.banner, sizeof(view_.banner), "Hold reset button %lus for factory reset", (unsigned long)holdSeconds);
}

void SupervisorHMIModule::loop()
{
    if (!driverReady_) {
        driverReady_ = driver_.begin();
        if (!driverReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
        lastRenderMs_ = 0;
        splashHoldUntilMs_ = millis() + kStartupSplashHoldMs;
        backlightForceOnUntilMs_ = millis() + kStartupBacklightForceOnMs;
        hasLastRenderKey_ = false;
        lastRenderedMinute_ = 0;
        lastRenderedPageCycle_ = 0;
        lastWifiRssiRefreshMs_ = 0;
        lastBacklightOn_ = driver_.isBacklightOn();
        cachedWifiHasRssi_ = false;
        cachedWifiRssiDbm_ = -127;
    }

    pollWifiAndNetwork_();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastFwPollMs_) >= kFwPollMs) {
        lastFwPollMs_ = now;
        pollFirmwareStatus_();
    }

    if (flowRuntimeDirty_) {
        flowRuntimeDirty_ = false;
        refreshFlowStatusFromDataStore_();
    }

    updateFactoryResetButton_();
    updateBacklight_();
    rebuildBanner_();

    view_.factoryResetPending = factoryResetPending_;

    if ((int32_t)(millis() - splashHoldUntilMs_) < 0) {
        vTaskDelay(pdMS_TO_TICKS(25));
        return;
    }

    const bool backlightOn = driver_.isBacklightOn();
    const uint32_t pageCycleKey = currentPageCycle_();
    view_.pageIndex = (uint8_t)(pageCycleKey & 0x01U);
    const uint32_t renderKey = buildRenderKey_();
    const uint32_t minuteKey = currentClockMinute_();
    const bool changed = (!hasLastRenderKey_) ||
                         (renderKey != lastRenderKey_) ||
                         (minuteKey != lastRenderedMinute_) ||
                         (pageCycleKey != lastRenderedPageCycle_) ||
                         (backlightOn != lastBacklightOn_);
    if (changed && backlightOn) {
        (void)driver_.render(view_, !hasLastRenderKey_);
        lastRenderMs_ = now;
        lastRenderKey_ = renderKey;
        lastRenderedMinute_ = minuteKey;
        lastRenderedPageCycle_ = pageCycleKey;
        hasLastRenderKey_ = true;
    }
    lastBacklightOn_ = backlightOn;

    executePendingFactoryReset_();

    vTaskDelay(pdMS_TO_TICKS(25));
}
