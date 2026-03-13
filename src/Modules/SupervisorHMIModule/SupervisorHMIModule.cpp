/**
 * @file SupervisorHMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

namespace {

#ifndef FLOW_SUPERVISOR_TFT_RESX
#define FLOW_SUPERVISOR_TFT_RESX 240
#endif
#ifndef FLOW_SUPERVISOR_TFT_RESY
#define FLOW_SUPERVISOR_TFT_RESY 320
#endif
#ifndef FLOW_SUPERVISOR_TFT_ROTATION
#define FLOW_SUPERVISOR_TFT_ROTATION 1
#endif
#ifndef FLOW_SUPERVISOR_TFT_COLSTART
#define FLOW_SUPERVISOR_TFT_COLSTART 0
#endif
#ifndef FLOW_SUPERVISOR_TFT_ROWSTART
#define FLOW_SUPERVISOR_TFT_ROWSTART 33
#endif
#ifndef FLOW_SUPERVISOR_TFT_BL
#define FLOW_SUPERVISOR_TFT_BL 14
#endif
#ifndef FLOW_SUPERVISOR_TFT_CS
#define FLOW_SUPERVISOR_TFT_CS 15
#endif
#ifndef FLOW_SUPERVISOR_TFT_DC
#define FLOW_SUPERVISOR_TFT_DC 4
#endif
#ifndef FLOW_SUPERVISOR_TFT_RST
#define FLOW_SUPERVISOR_TFT_RST 5
#endif
#ifndef FLOW_SUPERVISOR_TFT_MOSI
#define FLOW_SUPERVISOR_TFT_MOSI 19
#endif
#ifndef FLOW_SUPERVISOR_TFT_SCLK
#define FLOW_SUPERVISOR_TFT_SCLK 18
#endif
#ifndef FLOW_SUPERVISOR_TFT_COLOR_SWAP_BYTES
#define FLOW_SUPERVISOR_TFT_COLOR_SWAP_BYTES 1
#endif
#ifndef FLOW_SUPERVISOR_TFT_INVERT_COLORS
#define FLOW_SUPERVISOR_TFT_INVERT_COLORS 0
#endif
#ifndef FLOW_SUPERVISOR_TFT_SPI_HZ
#define FLOW_SUPERVISOR_TFT_SPI_HZ 40000000
#endif
#ifndef FLOW_SUPERVISOR_TFT_MIN_RENDER_GAP_MS
#define FLOW_SUPERVISOR_TFT_MIN_RENDER_GAP_MS 80
#endif

#ifndef FLOW_SUPERVISOR_PIR_PIN
#define FLOW_SUPERVISOR_PIR_PIN 36
#endif
#ifndef FLOW_SUPERVISOR_TFT_PIR_TIMEOUT_MS
#define FLOW_SUPERVISOR_TFT_PIR_TIMEOUT_MS 120000
#endif
#ifndef FLOW_SUPERVISOR_PIR_DEBOUNCE_MS
#define FLOW_SUPERVISOR_PIR_DEBOUNCE_MS 120
#endif
#ifndef FLOW_SUPERVISOR_PIR_ACTIVE_HIGH
#define FLOW_SUPERVISOR_PIR_ACTIVE_HIGH 1
#endif

#ifndef FLOW_SUPERVISOR_WIFI_RESET_PIN
#define FLOW_SUPERVISOR_WIFI_RESET_PIN 23
#endif
#ifndef FLOW_SUPERVISOR_WIFI_RESET_HOLD_MS
#define FLOW_SUPERVISOR_WIFI_RESET_HOLD_MS 3000
#endif
#ifndef FLOW_SUPERVISOR_WIFI_RESET_DEBOUNCE_MS
#define FLOW_SUPERVISOR_WIFI_RESET_DEBOUNCE_MS 40
#endif

static constexpr int8_t kPirPin = FLOW_SUPERVISOR_PIR_PIN;
static constexpr int8_t kWifiResetPin = FLOW_SUPERVISOR_WIFI_RESET_PIN;
static constexpr uint32_t kPirTimeoutMs = (uint32_t)FLOW_SUPERVISOR_TFT_PIR_TIMEOUT_MS;
static constexpr uint32_t kPirDebounceMs = (uint32_t)FLOW_SUPERVISOR_PIR_DEBOUNCE_MS;
static constexpr bool kPirActiveHigh = (FLOW_SUPERVISOR_PIR_ACTIVE_HIGH != 0);
static constexpr uint32_t kWifiResetHoldMs = (uint32_t)FLOW_SUPERVISOR_WIFI_RESET_HOLD_MS;
static constexpr uint32_t kWifiResetDebounceMs = (uint32_t)FLOW_SUPERVISOR_WIFI_RESET_DEBOUNCE_MS;
static constexpr uint32_t kFwPollMs = 500U;
static constexpr uint32_t kFlowPollMs = 2000U;
static constexpr uint32_t kStartupSplashHoldMs = 5000U;
static constexpr uint32_t kButtonBootGuardMs = 8000U;
static constexpr uint32_t kButtonArmHighStableMs = 500U;

} // namespace

SupervisorHMIModule::SupervisorHMIModule()
    : driver_(makeDriverConfig_())
{
}

St7789SupervisorDriverConfig SupervisorHMIModule::makeDriverConfig_()
{
    St7789SupervisorDriverConfig cfg{};
    cfg.resX = (uint16_t)FLOW_SUPERVISOR_TFT_RESX;
    cfg.resY = (uint16_t)FLOW_SUPERVISOR_TFT_RESY;
    cfg.rotation = (uint8_t)FLOW_SUPERVISOR_TFT_ROTATION;
    cfg.colStart = (int8_t)FLOW_SUPERVISOR_TFT_COLSTART;
    cfg.rowStart = (int8_t)FLOW_SUPERVISOR_TFT_ROWSTART;
    cfg.backlightPin = (int8_t)FLOW_SUPERVISOR_TFT_BL;
    cfg.csPin = (int8_t)FLOW_SUPERVISOR_TFT_CS;
    cfg.dcPin = (int8_t)FLOW_SUPERVISOR_TFT_DC;
    cfg.rstPin = (int8_t)FLOW_SUPERVISOR_TFT_RST;
    cfg.mosiPin = (int8_t)FLOW_SUPERVISOR_TFT_MOSI;
    cfg.sclkPin = (int8_t)FLOW_SUPERVISOR_TFT_SCLK;
    cfg.swapColorBytes = (FLOW_SUPERVISOR_TFT_COLOR_SWAP_BYTES != 0);
    cfg.invertColors = (FLOW_SUPERVISOR_TFT_INVERT_COLORS != 0);
    cfg.spiHz = (uint32_t)FLOW_SUPERVISOR_TFT_SPI_HZ;
    cfg.minRenderGapMs = (uint16_t)FLOW_SUPERVISOR_TFT_MIN_RENDER_GAP_MS;
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
    mix(view_.ip, strnlen(view_.ip, sizeof(view_.ip)) + 1U);
    mix(&view_.flowLinkOk, sizeof(view_.flowLinkOk));
    mix(&view_.flowHasPoolModes, sizeof(view_.flowHasPoolModes));
    mix(&view_.flowFiltrationAuto, sizeof(view_.flowFiltrationAuto));
    mix(&view_.flowWinterMode, sizeof(view_.flowWinterMode));
    mix(&view_.flowAlarmActiveCount, sizeof(view_.flowAlarmActiveCount));
    mix(&view_.flowAlarmCodeCount, sizeof(view_.flowAlarmCodeCount));
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); i++) {
        mix(view_.flowAlarmCodes[i], strnlen(view_.flowAlarmCodes[i], sizeof(view_.flowAlarmCodes[i])) + 1U);
    }
    mix(&view_.wifiResetPending, sizeof(view_.wifiResetPending));
    mix(view_.banner, strnlen(view_.banner, sizeof(view_.banner)) + 1U);

    return h;
}

uint32_t SupervisorHMIModule::currentClockMinute_() const
{
    const time_t now = time(nullptr);
    if (now <= 1600000000) return 0U;
    return (uint32_t)(now / 60);
}

void SupervisorHMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    wifiSvc_ = services.get<WifiService>("wifi");
    netAccessSvc_ = services.get<NetworkAccessService>("network_access");
    fwUpdateSvc_ = services.get<FirmwareUpdateService>("fwupdate");
    flowCfgSvc_ = services.get<FlowCfgRemoteService>("flowcfg");
    (void)logHub_;

    if (kPirPin >= 0) {
        int pirMode = INPUT;
#if defined(ESP32)
        // ESP32 GPIO 34..39 are input-only and don't support internal pull resistors.
        if (kPirPin <= 33) {
            pirMode = kPirActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP;
        }
#endif
        pinMode(kPirPin, pirMode);
        const bool pirLevelHigh = (digitalRead(kPirPin) == HIGH);
        const bool rawPir = kPirActiveHigh ? pirLevelHigh : !pirLevelHigh;
        pirRawState_ = rawPir;
        pirStableState_ = rawPir;
        pirDebounceChangedAtMs_ = millis();
    }
    if (kWifiResetPin >= 0) {
        pinMode(kWifiResetPin, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(kWifiResetPin) == LOW);
        buttonRawPressed_ = rawPressed;
        buttonStablePressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = millis();
    }

    lastMotionMs_ = millis();
    copyText_(view_.fwState, sizeof(view_.fwState), "idle");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), "none");
    copyText_(view_.banner, sizeof(view_.banner), "Hold WiFi button 3s to reset credentials");

    LOGI("Supervisor HMI initialized tft_cs=%d tft_dc=%d tft_rst=%d tft_bl=%d tft_mosi=%d tft_sclk=%d rot=%d colstart=%d rowstart=%d pir=%d pir_active_high=%d wifi_reset=%d",
         (int)FLOW_SUPERVISOR_TFT_CS,
         (int)FLOW_SUPERVISOR_TFT_DC,
         (int)FLOW_SUPERVISOR_TFT_RST,
         (int)FLOW_SUPERVISOR_TFT_BL,
         (int)FLOW_SUPERVISOR_TFT_MOSI,
         (int)FLOW_SUPERVISOR_TFT_SCLK,
         (int)FLOW_SUPERVISOR_TFT_ROTATION,
         (int)FLOW_SUPERVISOR_TFT_COLSTART,
         (int)FLOW_SUPERVISOR_TFT_ROWSTART,
         (int)kPirPin,
         (int)(kPirActiveHigh ? 1 : 0),
         (int)kWifiResetPin);
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
        view_.rssiDbm = (int32_t)WiFi.RSSI();
        view_.hasRssi = true;
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

void SupervisorHMIModule::pollFlowStatus_()
{
    view_.flowCfgReady = false;
    view_.flowLinkOk = false;
    view_.flowFirmware[0] = '\0';
    view_.flowHasRssi = false;
    view_.flowRssiDbm = -127;
    view_.flowHasHeapFrag = false;
    view_.flowHeapFragPct = 0;
    view_.flowMqttReady = false;
    view_.flowMqttRxDrop = 0;
    view_.flowMqttParseFail = 0;
    view_.flowI2cReqCount = 0;
    view_.flowI2cBadReqCount = 0;
    view_.flowI2cLastReqAgoMs = 0;
    view_.flowHasPoolModes = false;
    view_.flowFiltrationAuto = false;
    view_.flowWinterMode = false;
    view_.flowAlarmActiveCount = 0;
    view_.flowAlarmCodeCount = 0;
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); i++) {
        view_.flowAlarmCodes[i][0] = '\0';
    }

    if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusJson) return;
    if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) return;

    memset(flowStatusJsonBuf_, 0, sizeof(flowStatusJsonBuf_));
    if (!flowCfgSvc_->runtimeStatusJson(flowCfgSvc_->ctx, flowStatusJsonBuf_, sizeof(flowStatusJsonBuf_))) return;

    static bool filterInit = false;
    static StaticJsonDocument<512> filter;
    if (!filterInit) {
        filter["ok"] = true;
        filter["fw"] = true;
        filter["wifi"]["hrss"] = true;
        filter["wifi"]["rssi"] = true;
        filter["heap"]["frag"] = true;
        filter["mqtt"]["rdy"] = true;
        filter["mqtt"]["rxdrp"] = true;
        filter["mqtt"]["prsf"] = true;
        filter["i2c"]["lnk"] = true;
        filter["i2c"]["req"] = true;
        filter["i2c"]["breq"] = true;
        filter["i2c"]["ago"] = true;
        filter["pool"]["has"] = true;
        filter["pool"]["auto"] = true;
        filter["pool"]["wint"] = true;
        filter["alm"]["cnt"] = true;
        JsonArray filterCompactCodes = filter["alm"]["codes"].to<JsonArray>();
        filterCompactCodes.add(true);
        filterInit = true;
    }

    static StaticJsonDocument<1024> doc;
    doc.clear();
    const DeserializationError err = deserializeJson(doc,
                                                     flowStatusJsonBuf_,
                                                     DeserializationOption::Filter(filter));
    if (err || !doc.is<JsonObjectConst>()) return;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    const bool ok = root["ok"] | false;
    if (!ok) return;

    view_.flowCfgReady = true;
    copyText_(view_.flowFirmware, sizeof(view_.flowFirmware), root["fw"] | "");

    JsonObjectConst flowWifi = root["wifi"];
    view_.flowHasRssi = flowWifi["hrss"] | false;
    view_.flowRssiDbm = (int32_t)(flowWifi["rssi"] | -127);

    JsonObjectConst flowHeap = root["heap"];
    if (!flowHeap.isNull()) {
        view_.flowHasHeapFrag = true;
        view_.flowHeapFragPct = (uint8_t)(flowHeap["frag"] | 0U);
    }

    JsonObjectConst flowMqtt = root["mqtt"];
    view_.flowMqttReady = flowMqtt["rdy"] | false;
    view_.flowMqttRxDrop = (uint32_t)(flowMqtt["rxdrp"] | 0U);
    view_.flowMqttParseFail = (uint32_t)(flowMqtt["prsf"] | 0U);

    JsonObjectConst i2c = root["i2c"];
    view_.flowLinkOk = i2c["lnk"] | false;
    view_.flowI2cReqCount = (uint32_t)(i2c["req"] | 0U);
    view_.flowI2cBadReqCount = (uint32_t)(i2c["breq"] | 0U);
    view_.flowI2cLastReqAgoMs = (uint32_t)(i2c["ago"] | 0U);

    JsonObjectConst poolMode = root["pool"];
    if (!poolMode.isNull()) {
        view_.flowHasPoolModes = poolMode["has"] | false;
        view_.flowFiltrationAuto = poolMode["auto"] | false;
        view_.flowWinterMode = poolMode["wint"] | false;
    }

    JsonObjectConst alarms = root["alm"];
    if (!alarms.isNull()) {
        view_.flowAlarmActiveCount = (uint8_t)(alarms["cnt"] | 0U);
        JsonArrayConst activeCodes = alarms["codes"].as<JsonArrayConst>();
        for (JsonVariantConst codeV : activeCodes) {
            const size_t idx = (size_t)view_.flowAlarmCodeCount;
            if (idx >= (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0]))) break;
            const char* code = codeV.as<const char*>();
            copyText_(view_.flowAlarmCodes[idx], sizeof(view_.flowAlarmCodes[idx]), code ? code : "");
            view_.flowAlarmCodeCount++;
        }
    }
}

void SupervisorHMIModule::triggerWifiReset_()
{
    if (wifiResetPending_) return;
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: config unavailable");
        return;
    }

    static constexpr const char* kWifiResetPatch = "{\"wifi\":{\"enabled\":true,\"ssid\":\"\",\"pass\":\"\"}}";
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, kWifiResetPatch);
    if (!ok) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: applyJson");
        return;
    }

    if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
        (void)netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }

    wifiResetPending_ = true;
    restartAtMs_ = millis() + 1200U;
    copyText_(view_.banner, sizeof(view_.banner), "WiFi credentials reset: rebooting...");
    LOGW("WiFi credentials reset requested from local button");
}

void SupervisorHMIModule::updateWifiResetButton_()
{
    if (kWifiResetPin < 0) return;

    const uint32_t now = millis();
    const bool rawPressed = (digitalRead(kWifiResetPin) == LOW);

    if (rawPressed != buttonRawPressed_) {
        buttonRawPressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = now;
    }
    if ((uint32_t)(now - buttonDebounceChangedAtMs_) >= kWifiResetDebounceMs) {
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
        if (now < kButtonBootGuardMs) {
            return;
        }

        buttonArmed_ = true;
        LOGI("WiFi reset button armed");
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
        return;
    }

    if (buttonTriggered_) return;
    if ((uint32_t)(now - buttonPressedAtMs_) < kWifiResetHoldMs) return;

    buttonTriggered_ = true;
    triggerWifiReset_();
}

void SupervisorHMIModule::updateBacklight_()
{
    const uint32_t now = millis();
    bool motion = false;
    if (kPirPin >= 0) {
        const bool pirLevelHigh = (digitalRead(kPirPin) == HIGH);
        const bool rawMotion = kPirActiveHigh ? pirLevelHigh : !pirLevelHigh;
        if (rawMotion != pirRawState_) {
            pirRawState_ = rawMotion;
            pirDebounceChangedAtMs_ = now;
        }
        if ((uint32_t)(now - pirDebounceChangedAtMs_) >= kPirDebounceMs) {
            pirStableState_ = pirRawState_;
        }
        motion = pirStableState_;
    }
    if (motion || fwBusyOrPending_) {
        lastMotionMs_ = now;
    }

    if (kPirPin < 0) {
        driver_.setBacklight(true);
        return;
    }

    const bool keepOn = fwBusyOrPending_ || ((uint32_t)(now - lastMotionMs_) <= kPirTimeoutMs);
    driver_.setBacklight(keepOn);
}

void SupervisorHMIModule::rebuildBanner_()
{
    if (wifiResetPending_) return;
    if (!driver_.isBacklightOn()) {
        copyText_(view_.banner, sizeof(view_.banner), "PIR idle: backlight off");
        return;
    }
    copyText_(view_.banner, sizeof(view_.banner), "Hold WiFi button 3s to reset credentials");
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
        hasLastRenderKey_ = false;
        lastRenderedMinute_ = 0;
        lastBacklightOn_ = driver_.isBacklightOn();
    }

    pollWifiAndNetwork_();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastFwPollMs_) >= kFwPollMs) {
        lastFwPollMs_ = now;
        pollFirmwareStatus_();
    }
    if ((uint32_t)(now - lastFlowPollMs_) >= kFlowPollMs) {
        lastFlowPollMs_ = now;
        pollFlowStatus_();
    }

    updateWifiResetButton_();
    updateBacklight_();
    rebuildBanner_();

    view_.wifiResetPending = wifiResetPending_;

    if ((int32_t)(millis() - splashHoldUntilMs_) < 0) {
        vTaskDelay(pdMS_TO_TICKS(25));
        return;
    }

    const bool backlightOn = driver_.isBacklightOn();
    const uint32_t renderKey = buildRenderKey_();
    const uint32_t minuteKey = currentClockMinute_();
    const bool changed = (!hasLastRenderKey_) ||
                         (renderKey != lastRenderKey_) ||
                         (minuteKey != lastRenderedMinute_) ||
                         (backlightOn != lastBacklightOn_);
    if (changed && backlightOn) {
        (void)driver_.render(view_, !hasLastRenderKey_);
        lastRenderMs_ = now;
        lastRenderKey_ = renderKey;
        lastRenderedMinute_ = minuteKey;
        hasLastRenderKey_ = true;
    }
    lastBacklightOn_ = backlightOn;

    if (restartAtMs_ != 0U && (int32_t)(millis() - restartAtMs_) >= 0) {
        delay(30);
        esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(25));
}
