/**
 * @file HMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/HMIModule.h"

#include "Board/BoardSerialMap.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/AlarmIds.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

namespace {

#ifndef FLOW_HMI_CONFIG_MENU_ENABLED
#define FLOW_HMI_CONFIG_MENU_ENABLED 0
#endif
static constexpr bool kConfigMenuEnabled = (FLOW_HMI_CONFIG_MENU_ENABLED != 0);
static constexpr uint8_t kLedBitMqttConnected = 0;
static constexpr uint8_t kLedBitPageSelect = 1;
static constexpr uint8_t kLedBitModeA = 2;
static constexpr uint8_t kLedBitModeB = 3;
static constexpr uint8_t kLedBitAlarmA = 4;
static constexpr uint8_t kLedBitAlarmB = 5;
static constexpr uint8_t kLedBitAlarmC = 6;
static constexpr uint8_t kLedBitAlarmD = 7;
static constexpr uint32_t kLedPageTogglePeriodMs = 2000U;

static inline uint8_t normalizeLedPage_(uint8_t page)
{
    return (page == 2U) ? 2U : 1U;
}

static bool findJsonBool_(const char* json, const char* key, bool& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (strncmp(p, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

#if FLOW_HMI_CONFIG_MENU_ENABLED
static const ConfigMenuHint kHints[] = {
    {"poollogic", "filtr_start_min", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic", "filtr_stop_max", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic", "ph_setpoint", {ConfigMenuWidget::Slider, true, 6.6f, 7.8f, 0.1f, nullptr}},
    {"poollogic", "orp_setpoint", {ConfigMenuWidget::Slider, true, 450.0f, 950.0f, 10.0f, nullptr}},
    {"time", "tz", {ConfigMenuWidget::Select, true, 0.0f, 0.0f, 1.0f,
                    "CET-1CEST,M3.5.0/2,M10.5.0/3|UTC0|EST5EDT,M3.2.0/2,M11.1.0/2"}}
};
#endif

} // namespace

bool HMIModule::svcRequestRefresh_(void* ctx)
{
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    self->viewDirty_ = true;
    return true;
}

bool HMIModule::svcOpenConfigHome_(void* ctx)
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    const bool ok = self->menu_.home();
    if (ok) self->viewDirty_ = true;
    return ok;
#else
    (void)ctx;
    return false;
#endif
}

bool HMIModule::svcOpenConfigModule_(void* ctx, const char* module)
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    const bool ok = self->menu_.openModule(module);
    if (ok) self->viewDirty_ = true;
    return ok;
#else
    (void)ctx;
    (void)module;
    return false;
#endif
}

bool HMIModule::svcBuildConfigMenuJson_(void* ctx, char* out, size_t outLen)
{
    const HMIModule* self = static_cast<const HMIModule*>(ctx);
    if (!self) return false;
    return self->buildMenuJson_(out, outLen);
}

bool HMIModule::svcSetLedPage_(void* ctx, uint8_t page)
{
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    const uint8_t newPage = normalizeLedPage_(page);
    if (self->ledPage_ != newPage) {
        self->ledPage_ = newPage;
        self->applyLedMask_(true);
    }
    return true;
}

uint8_t HMIModule::svcGetLedPage_(void* ctx)
{
    const HMIModule* self = static_cast<const HMIModule*>(ctx);
    if (!self) return 1U;
    return self->ledPage_;
}

void HMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    dsSvc_ = services.get<DataStoreService>("datastore");
    alarmSvc_ = services.get<AlarmService>("alarms");
    statusLedsSvc_ = services.get<StatusLedsService>("status_leds");
    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (!cfgSvc_) {
        LOGE("Config service unavailable");
        return;
    }

#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        const bool okMenu = menu_.begin(cfgSvc_);
        menu_.setHints(kHints, (uint8_t)(sizeof(kHints) / sizeof(kHints[0])));
        if (!okMenu) {
            LOGE("Config menu init failed");
        }
    }
#else
    {
        LOGI("Config menu disabled at compile-time");
    }
#endif

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &HMIModule::onEventStatic_, this);
    }

    static HmiService svc{
        svcRequestRefresh_,
        svcOpenConfigHome_,
        svcOpenConfigModule_,
        svcBuildConfigMenuJson_,
        svcSetLedPage_,
        svcGetLedPage_,
        nullptr
    };
    svc.ctx = this;
    services.add("hmi", &svc);

    NextionDriverConfig dcfg{};
    dcfg.serial = &Board::SerialMap::hmiSerial();
    dcfg.rxPin = Board::SerialMap::hmiRxPin();
    dcfg.txPin = Board::SerialMap::hmiTxPin();
    dcfg.baud = Board::SerialMap::HmiBaud;
    nextion_.setConfig(dcfg);
    driver_ = &nextion_;
    driverReady_ = false;
    viewDirty_ = true;
    lastRenderMs_ = 0;
    ledPage_ = 1U;
    ledMaskValid_ = false;
    lastLedApplyTryMs_ = 0;
    lastLedPageToggleMs_ = millis();

    refreshPoolLogicFlags_();
    refreshRuntimeFlags_();
    refreshAlarmFlags_();
    applyLedMask_(true);

    LOGI("HMI service registered with driver=%s led_panel=%s",
         driver_ ? driver_->driverId() : "none",
         statusLedsSvc_ ? "on" : "off");
}

void HMIModule::onEventStatic_(const Event& e, void* user)
{
    HMIModule* self = static_cast<HMIModule*>(user);
    if (self) self->onEvent_(e);
}

void HMIModule::refreshPoolLogicFlags_()
{
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return;
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx,
                               "poollogic/mode",
                               poollogicCfgJson_,
                               sizeof(poollogicCfgJson_),
                               &truncated)) {
        return;
    }
    (void)truncated;

    bool v = false;
    if (findJsonBool_(poollogicCfgJson_, "auto_mode", v)) autoRegEnabled_ = v;
    if (findJsonBool_(poollogicCfgJson_, "winter_mode", v)) winterMode_ = v;
    if (findJsonBool_(poollogicCfgJson_, "ph_auto_mode", v)) phPidEnabled_ = v;
    if (findJsonBool_(poollogicCfgJson_, "orp_auto_mode", v)) chlorinePidEnabled_ = v;
}

void HMIModule::updatePumpRuntimeAlarmFromSlot_(uint8_t slot)
{
    if (!dsSvc_ || !dsSvc_->store) return;
    PoolDeviceRuntimeStateEntry entry{};
    const bool active =
        poolDeviceRuntimeState(*dsSvc_->store, slot, entry) &&
        entry.valid &&
        entry.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME;

    if (slot == PoolBinding::kDeviceSlotPhPump) {
        phPumpRuntimeAlarm_ = active;
    } else if (slot == PoolBinding::kDeviceSlotChlorinePump) {
        chlorinePumpRuntimeAlarm_ = active;
    }
}

void HMIModule::refreshRuntimeFlags_()
{
    if (!dsSvc_ || !dsSvc_->store) return;
    mqttReady_ = mqttReady(*dsSvc_->store);
    updatePumpRuntimeAlarmFromSlot_(PoolBinding::kDeviceSlotPhPump);
    updatePumpRuntimeAlarmFromSlot_(PoolBinding::kDeviceSlotChlorinePump);
}

void HMIModule::refreshAlarmFlags_()
{
    if (!alarmSvc_ || !alarmSvc_->isActive) return;
    void* ctx = alarmSvc_->ctx;
    phTankLowAlarm_ = alarmSvc_->isActive(ctx, AlarmId::PoolPhTankLow);
    chlorineTankLowAlarm_ = alarmSvc_->isActive(ctx, AlarmId::PoolChlorineTankLow);
    psiAlarm_ = alarmSvc_->isActive(ctx, AlarmId::PoolPsiLow) ||
                alarmSvc_->isActive(ctx, AlarmId::PoolPsiHigh);
}

void HMIModule::applyLedMask_(bool force)
{
    if (!statusLedsSvc_ || !statusLedsSvc_->setMask) return;

    uint8_t mask = 0U;
    if (mqttReady_) mask |= (uint8_t)(1U << kLedBitMqttConnected);

    const bool page2 = (ledPage_ == 2U);
    if (page2) mask |= (uint8_t)(1U << kLedBitPageSelect);

    if (!page2) {
        if (autoRegEnabled_) mask |= (uint8_t)(1U << kLedBitModeA);
        if (winterMode_) mask |= (uint8_t)(1U << kLedBitModeB);
        if (phTankLowAlarm_) mask |= (uint8_t)(1U << kLedBitAlarmA);
        if (chlorineTankLowAlarm_) mask |= (uint8_t)(1U << kLedBitAlarmB);
        if (phPumpRuntimeAlarm_) mask |= (uint8_t)(1U << kLedBitAlarmC);
        if (chlorinePumpRuntimeAlarm_) mask |= (uint8_t)(1U << kLedBitAlarmD);
    } else {
        if (phPidEnabled_) mask |= (uint8_t)(1U << kLedBitModeA);
        if (chlorinePidEnabled_) mask |= (uint8_t)(1U << kLedBitModeB);
        if (psiAlarm_) mask |= (uint8_t)(1U << kLedBitAlarmD);
    }

    if (!force && ledMaskValid_ && ledMaskLast_ == mask) return;
    if (statusLedsSvc_->setMask(statusLedsSvc_->ctx, mask, millis())) {
        ledMaskLast_ = mask;
        ledMaskValid_ = true;
    }
}

bool HMIModule::refreshCurrentModule_()
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    return false;
#else
    const uint8_t prevPage = menu_.pageIndex();
    if (!menu_.refreshCurrent()) return false;
    while (menu_.pageIndex() < prevPage && menu_.nextPage()) {
    }
    return true;
#endif
}

void HMIModule::onEvent_(const Event& e)
{
    bool ledDirty = false;

    if (e.id == EventId::ConfigChanged && e.payload && e.len >= sizeof(ConfigChangedPayload)) {
        const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
#if FLOW_HMI_CONFIG_MENU_ENABLED
        if (!menu_.isHome() && p->module[0] && strcmp(p->module, menu_.currentModule()) == 0) {
            if (refreshCurrentModule_()) viewDirty_ = true;
        } else if (menu_.isHome()) {
            viewDirty_ = true;
        }
#endif
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic) {
            refreshPoolLogicFlags_();
            ledDirty = true;
        }
    } else if (e.id == EventId::DataChanged && e.payload && e.len >= sizeof(DataChangedPayload)) {
        const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
        if (p->id == DATAKEY_MQTT_READY) {
            refreshRuntimeFlags_();
            ledDirty = true;
        } else if (p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + PoolBinding::kDeviceSlotPhPump)) {
            updatePumpRuntimeAlarmFromSlot_(PoolBinding::kDeviceSlotPhPump);
            ledDirty = true;
        } else if (p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + PoolBinding::kDeviceSlotChlorinePump)) {
            updatePumpRuntimeAlarmFromSlot_(PoolBinding::kDeviceSlotChlorinePump);
            ledDirty = true;
        }
    } else if ((e.id == EventId::AlarmRaised || e.id == EventId::AlarmCleared) &&
               e.payload &&
               e.len >= sizeof(AlarmPayload)) {
        const AlarmPayload* p = static_cast<const AlarmPayload*>(e.payload);
        const AlarmId id = (AlarmId)p->alarmId;
        if (id == AlarmId::PoolPhTankLow) {
            phTankLowAlarm_ = (e.id == EventId::AlarmRaised);
            ledDirty = true;
        } else if (id == AlarmId::PoolChlorineTankLow) {
            chlorineTankLowAlarm_ = (e.id == EventId::AlarmRaised);
            ledDirty = true;
        } else if (id == AlarmId::PoolPsiLow || id == AlarmId::PoolPsiHigh) {
            if (e.id == EventId::AlarmRaised) {
                psiAlarm_ = true;
            } else {
                refreshAlarmFlags_();
            }
            ledDirty = true;
        }
    }

    if (ledDirty) {
        applyLedMask_();
    }
}

void HMIModule::handleDriverEvent_(const HmiEvent& e)
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (e.type == HmiEventType::NextPage) {
            ledPage_ = 2U;
            applyLedMask_(true);
        } else if (e.type == HmiEventType::PrevPage ||
                   e.type == HmiEventType::Home ||
                   e.type == HmiEventType::Back) {
            ledPage_ = 1U;
            applyLedMask_(true);
        }
        return;
    }
#else
    bool changed = false;
    switch (e.type) {
        case HmiEventType::Home:
            changed = menu_.home();
            break;
        case HmiEventType::Back:
            changed = menu_.back();
            break;
        case HmiEventType::Validate: {
            char ack[96]{};
            changed = menu_.validate(ack, sizeof(ack));
            if (!changed) {
                LOGW("Validate failed");
            }
            break;
        }
        case HmiEventType::NextPage:
            changed = menu_.nextPage();
            break;
        case HmiEventType::PrevPage:
            changed = menu_.prevPage();
            break;
        case HmiEventType::RowActivate: {
            changed = menu_.enterRow(e.row);
            if (!changed) {
                ConfigMenuView view{};
                menu_.buildView(view);
                if (e.row < ConfigMenuModel::RowsPerPage && view.rows[e.row].visible) {
                    const ConfigMenuWidget widget = view.rows[e.row].widget;
                    if (widget == ConfigMenuWidget::Switch) changed = menu_.toggleSwitch(e.row);
                    else if (widget == ConfigMenuWidget::Select) changed = menu_.cycleSelect(e.row, 1);
                }
            }
            break;
        }
        case HmiEventType::RowToggle:
            changed = menu_.toggleSwitch(e.row);
            break;
        case HmiEventType::RowCycle:
            changed = menu_.cycleSelect(e.row, e.direction);
            break;
        case HmiEventType::RowSetText:
            changed = menu_.setText(e.row, e.text);
            break;
        case HmiEventType::RowSetSlider:
            changed = menu_.setSlider(e.row, e.sliderValue);
            break;
        case HmiEventType::None:
        default:
            break;
    }

    if (changed) viewDirty_ = true;
#endif
}

bool HMIModule::render_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        menu_.buildView(view);
    }
#else
    {
        snprintf(view.breadcrumb, sizeof(view.breadcrumb), "Configuration indisponible");
        view.pageIndex = 0;
        view.pageCount = 1;
        view.canHome = false;
        view.canBack = false;
        view.canValidate = false;
        view.isHome = true;
        view.rowCountOnPage = 1;
        view.rows[0].visible = true;
        view.rows[0].editable = false;
        snprintf(view.rows[0].key, sizeof(view.rows[0].key), "menu");
        snprintf(view.rows[0].label, sizeof(view.rows[0].label), "Config");
        snprintf(view.rows[0].value, sizeof(view.rows[0].value), "disabled");
    }
#endif
    const bool ok = driver_->renderConfigMenu(view);
    if (ok) {
        lastRenderMs_ = millis();
    }
    return ok;
}

bool HMIModule::buildMenuJson_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;

#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        DynamicJsonDocument doc(256);
        JsonObject root = doc.to<JsonObject>();
        root["ok"] = true;
        root["disabled"] = true;
        root["driver"] = driver_ ? driver_->driverId() : "";
        root["path"] = "Configuration indisponible";
        root["page"] = 1U;
        root["pages"] = 1U;
        root["rows"] = 0U;
        root["can_home"] = false;
        root["can_back"] = false;
        root["can_validate"] = false;
        const size_t written = serializeJson(root, out, outLen);
        return written > 0 && written < outLen;
    }
#else
    ConfigMenuView view{};
    menu_.buildView(view);

    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    root["ok"] = true;
    root["driver"] = driver_ ? driver_->driverId() : "";
    root["path"] = view.breadcrumb;
    root["page"] = (uint32_t)view.pageIndex + 1U;
    root["pages"] = (uint32_t)view.pageCount;
    root["rows"] = (uint32_t)view.rowCountOnPage;
    root["can_home"] = view.canHome;
    root["can_back"] = view.canBack;
    root["can_validate"] = view.canValidate;

    JsonArray arr = root.createNestedArray("items");
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        if (!row.visible) continue;
        JsonObject it = arr.createNestedObject();
        it["i"] = i;
        it["key"] = row.key;
        it["label"] = row.label;
        it["value"] = row.value;
        it["editable"] = row.editable;
        it["dirty"] = row.dirty;
        it["widget"] = (uint8_t)row.widget;
    }

    const size_t written = serializeJson(root, out, outLen);
    return written > 0 && written < outLen;
#endif
}

void HMIModule::loop()
{
    if (!driver_) {
        vTaskDelay(pdMS_TO_TICKS(250));
        return;
    }

    if (!driverReady_) {
        driverReady_ = driver_->begin();
        if (!driverReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
        viewDirty_ = true;
    }

    HmiEvent ev{};
    while (driver_->pollEvent(ev)) {
        handleDriverEvent_(ev);
    }

    const uint32_t now = millis();
    if (statusLedsSvc_ && (uint32_t)(now - lastLedPageToggleMs_) >= kLedPageTogglePeriodMs) {
        lastLedPageToggleMs_ = now;
        const uint8_t nextPage = (ledPage_ == 1U) ? 2U : 1U;
        ledPage_ = nextPage;
        applyLedMask_(true);
    }
    if (!ledMaskValid_ || (uint32_t)(now - lastLedApplyTryMs_) >= 1000U) {
        applyLedMask_();
        lastLedApplyTryMs_ = now;
    }
    const bool periodicRefresh = ((uint32_t)(now - lastRenderMs_) >= 1200U);
    if (viewDirty_ || periodicRefresh) {
        if (render_()) {
            viewDirty_ = false;
        }
    }

    driver_->tick(now);
    vTaskDelay(pdMS_TO_TICKS(25));
}
