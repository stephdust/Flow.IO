/**
 * @file SystemModule.cpp
 * @brief Implementation file.
 */
#include "SystemModule.h"
#include "Core/ErrorCodes.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/FirmwareVersion.h"
#include "Core/SystemStats.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <string.h>
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::SystemModule)
#include "Core/ModuleLog.h"

static bool wipeWifiPersistent_(esp_err_t* outErr)
{
    // Keep WiFi driver initialized while clearing persisted station/AP data.
    WiFi.mode(WIFI_MODE_STA);
    delay(20);
    (void)WiFi.disconnect(false, true); // keep radio on, erase AP credentials

    esp_err_t err = esp_wifi_restore();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        // Retry once after forcing STA mode in case the driver was not up yet.
        WiFi.mode(WIFI_MODE_STA);
        delay(20);
        err = esp_wifi_restore();
    }

    // Best-effort shutdown after erase.
    (void)WiFi.disconnect(true, true);

    if (outErr) *outErr = err;
    return (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT);
}

static bool writeOkReply_(char* reply, size_t replyLen, const char* json, const char* where)
{
    if (!reply || replyLen == 0 || !json) return false;
    const int wrote = snprintf(reply, replyLen, "%s", json);
    if (wrote > 0 && (size_t)wrote < replyLen) return true;
    if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
    return false;
}

bool SystemModule::cmdPing(void*, const CommandRequest&, char* reply, size_t replyLen) {
    return writeOkReply_(reply, replyLen, "{\"ok\":true,\"pong\":true}", "system.ping");
}

bool SystemModule::cmdReboot(void*, const CommandRequest&, char* reply, size_t replyLen) {
    if (!writeOkReply_(reply, replyLen, "{\"ok\":true,\"msg\":\"rebooting\"}", "system.reboot")) {
        return false;
    }
    delay(200); ///< laisser le temps à MQTT de publier l'ACK
    esp_restart();
    return true;
}

bool SystemModule::cmdFactoryReset(void* userCtx, const CommandRequest&, char* reply, size_t replyLen) {
    SystemModule* self = static_cast<SystemModule*>(userCtx);
    if (!self || !self->cfgSvc || !self->cfgSvc->erase) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::NotReady, "system.factory_reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    const bool flowCfgCleared = self->cfgSvc->erase(self->cfgSvc->ctx);

    // Also wipe WiFi driver persisted credentials/settings from default NVS storage.
    esp_err_t wifiRestoreErr = ESP_OK;
    const bool wifiCfgCleared = wipeWifiPersistent_(&wifiRestoreErr);
    if (wifiRestoreErr == ESP_ERR_WIFI_NOT_INIT) {
        LOGW("WiFi driver not initialized during factory reset; restore skipped");
    }

    if (!flowCfgCleared || !wifiCfgCleared) {
        LOGE("Factory reset failed flow_cfg=%d wifi_cfg=%d wifi_err=%d",
             (int)flowCfgCleared,
             (int)wifiCfgCleared,
             (int)wifiRestoreErr);
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "system.factory_reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    if (!writeOkReply_(reply, replyLen, "{\"ok\":true,\"msg\":\"factory_reset\"}", "system.factory_reset")) {
        return false;
    }

    LOGI("Factory reset done flow_cfg=%d wifi_cfg=%d wifi_err=%d",
         (int)flowCfgCleared,
         (int)wifiCfgCleared,
         (int)wifiRestoreErr);

    delay(300); ///< laisser le temps à MQTT/Web de publier l'ACK
    esp_restart();
    return true;
}

bool SystemModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    switch (valueId) {
        case RuntimeUiFirmware:
            return writer.writeString(runtimeId, FirmwareVersion::Full);
        case RuntimeUiUptimeMs:
            return writer.writeU32(runtimeId, snap.uptimeMs);
        case RuntimeUiHeapFree:
            return writer.writeU32(runtimeId, snap.heap.freeBytes);
        case RuntimeUiHeapMinFree:
            return writer.writeU32(runtimeId, snap.heap.minFreeBytes);
        default:
            return false;
    }
}

bool SystemModule::isLangCode_(const char* value, char c0, char c1)
{
    if (!value) return false;
    char a = value[0];
    char b = value[1];
    if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
    if (a != c0 || b != c1) return false;
    const char end = value[2];
    return end == '\0' || end == '-' || end == '_' || end == '.';
}

bool SystemModule::normalizeLanguage_(bool bumpGenerationIfChanged)
{
    const char* normalized = "fr";
    if (isLangCode_(cfgData_.lang, 'e', 'n')) {
        normalized = "en";
    } else if (isLangCode_(cfgData_.lang, 'f', 'r')) {
        normalized = "fr";
    }

    const bool changed = (strncmp(cfgData_.lang, normalized, sizeof(cfgData_.lang)) != 0);
    if (changed) {
        snprintf(cfgData_.lang, sizeof(cfgData_.lang), "%s", normalized);
    }
    if (changed && bumpGenerationIfChanged) {
        ++localeGeneration_;
    }
    return changed;
}

void SystemModule::onEventStatic_(const Event& e, void* user)
{
    SystemModule* self = static_cast<SystemModule*>(user);
    if (self) self->onEvent_(e);
}

void SystemModule::onEvent_(const Event& e)
{
    if (e.id != EventId::ConfigChanged) return;
    if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;

    const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
    if (p->moduleId != (uint8_t)ConfigModuleId::System) return;
    if (strcmp(p->nvsKey, NvsKeys::System::Language) != 0) return;
    (void)normalizeLanguage_(true);
}

const char* SystemModule::localeLanguage_() const
{
    return cfgData_.lang;
}

uint32_t SystemModule::localeGenerationValue_() const
{
    return localeGeneration_;
}

void SystemModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::System;
    constexpr uint8_t kCfgBranchId = 1;

    cfg.registerVar(languageVar_, kCfgModuleId, kCfgBranchId);

    logHub = services.get<LogHubService>(ServiceId::LogHub);
    cmdSvc = services.get<CommandService>(ServiceId::Command);
    cfgSvc = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    eventBusSvc_ = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eventBusSvc_ ? eventBusSvc_->bus : nullptr;

    if (!services.add(ServiceId::Locale, &localeSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Locale));
    }

    cmdSvc->registerHandler(cmdSvc->ctx, "system.ping", cmdPing, this);
    cmdSvc->registerHandler(cmdSvc->ctx, "system.reboot", cmdReboot, this);
    cmdSvc->registerHandler(cmdSvc->ctx, "system.factory_reset", cmdFactoryReset, this);

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &SystemModule::onEventStatic_, this);
    }

    LOGI("Commands registered: system.ping system.reboot system.factory_reset");
}

void SystemModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    (void)normalizeLanguage_(false);
}
