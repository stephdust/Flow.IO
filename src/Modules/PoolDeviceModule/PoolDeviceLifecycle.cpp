/**
 * @file PoolDeviceLifecycle.cpp
 * @brief Lifecycle entry points and integration wiring for PoolDeviceModule.
 */

#include "PoolDeviceModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"
#include "Core/NvsKeys.h"
#include "Domain/Pool/PoolBindings.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include <new>

namespace {
static constexpr uint8_t kPoolDeviceCfgProducerId = 48;
static constexpr const char* kPoolDeviceCfgTopicBase = "cfg/pdm";
static constexpr const char* kPoolDeviceCfgRuntimeTopicBase = "cfg/pdmrt";
static constexpr uint8_t kPoolDeviceCfgBranchBase = 1;
static constexpr uint8_t kPoolDeviceCfgRuntimeBranchBase = (uint8_t)(kPoolDeviceCfgBranchBase + POOL_DEVICE_MAX);
static constexpr uint8_t kTimeCfgBranch = 1;
static constexpr uint16_t kCfgMsgBasePdm = 1;
static constexpr uint16_t kCfgMsgBasePdmrt = 2;
static constexpr uint16_t kCfgMsgPdmSlotBase = 16;
static constexpr uint16_t kCfgMsgPdmrtSlotBase = 32;

template <size_t Rows, size_t Cols>
size_t charTableUsage_(const char (&table)[Rows][Cols])
{
    size_t total = 0U;
    for (size_t row = 0; row < Rows; ++row) {
        const size_t len = strnlen(table[row], Cols);
        if (len > 0U) total += len + 1U;
    }
    return total;
}

static constexpr uint8_t poolDeviceCfgBranchFromSlot_(uint8_t slot)
{
    return (slot < POOL_DEVICE_MAX) ? (uint8_t)(kPoolDeviceCfgBranchBase + slot) : 0;
}

static constexpr uint8_t poolDeviceCfgRuntimeBranchFromSlot_(uint8_t slot)
{
    return (slot < POOL_DEVICE_MAX) ? (uint8_t)(kPoolDeviceCfgRuntimeBranchBase + slot) : 0;
}
} // namespace

void PoolDeviceModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolDeviceModule*>(user)->onEvent_(e);
}

void PoolDeviceModule::onEvent_(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (p->id != DATAKEY_TIME_READY) return;
        if (!dataStore_) return;
        if (timeReady(*dataStore_)) requestPeriodReconcile_();
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->moduleId == (uint8_t)ConfigModuleId::Time &&
            p->localBranchId == kTimeCfgBranch) {
            requestPeriodReconcile_();
        }
        return;
    }

    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    if ((SchedulerEdge)p->edge != SchedulerEdge::Trigger) return;

    uint8_t pending = 0;
    if (p->eventId == TIME_EVENT_SYS_DAY_START) {
        pending = RESET_PENDING_DAY;
    } else if (p->eventId == TIME_EVENT_SYS_WEEK_START) {
        pending = RESET_PENDING_WEEK;
    } else if (p->eventId == TIME_EVENT_SYS_MONTH_START) {
        pending = RESET_PENDING_MONTH;
    } else {
        return;
    }

    portENTER_CRITICAL(&resetMux_);
    resetPendingMask_ |= pending;
    portEXIT_CRITICAL(&resetMux_);
}

void PoolDeviceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolDevice;
    const uint8_t cfgRouteCap = (uint8_t)(2U + POOL_DEVICE_MAX * 2U);
    cfgStore_ = &cfg;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    if (!services.add(ServiceId::PoolDevice, &poolSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::PoolDevice));
    }
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    haSvc_ = services.get<HAService>(ServiceId::Ha);
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    if (!ioSvc_) {
        LOGW("PoolDevice waiting for IOServiceV2");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolDeviceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &PoolDeviceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolDeviceModule::onEventStatic_, this);
    }

    if (!cfgRoutes_) {
        cfgRoutes_ = new (std::nothrow) MqttConfigRouteProducer::Route[cfgRouteCap];
    }
    cfgRouteCount_ = 0;
    if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
        MqttConfigRouteProducer::Route& cfgBaseRoute = cfgRoutes_[cfgRouteCount_++];
        cfgBaseRoute = MqttConfigRouteProducer::Route{};
        cfgBaseRoute.messageId = kCfgMsgBasePdm;
        cfgBaseRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, ConfigBranchRef::UnknownLocalBranch};
        cfgBaseRoute.topicSuffix = "";
        cfgBaseRoute.changePriority = (uint8_t)MqttPublishPriority::Low;
        cfgBaseRoute.customBuild = &PoolDeviceModule::buildCfgBasePdmStatic_;
        cfgBaseRoute.topicBase = kPoolDeviceCfgTopicBase;
    }
    if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
        MqttConfigRouteProducer::Route& cfgRuntimeBaseRoute = cfgRoutes_[cfgRouteCount_++];
        cfgRuntimeBaseRoute = MqttConfigRouteProducer::Route{};
        cfgRuntimeBaseRoute.messageId = kCfgMsgBasePdmrt;
        cfgRuntimeBaseRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, ConfigBranchRef::UnknownLocalBranch};
        cfgRuntimeBaseRoute.topicSuffix = "";
        cfgRuntimeBaseRoute.changePriority = (uint8_t)MqttPublishPriority::Low;
        cfgRuntimeBaseRoute.customBuild = &PoolDeviceModule::buildCfgBasePdmrtStatic_;
        cfgRuntimeBaseRoute.topicBase = kPoolDeviceCfgRuntimeTopicBase;
    }

    // Each slot owns one config branch and one runtime-persist branch.
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        const uint8_t localBranchId = poolDeviceCfgBranchFromSlot_(i);
        const uint8_t runtimeBranchId = poolDeviceCfgRuntimeBranchFromSlot_(i);

        snprintf(cfgModuleName_[i], sizeof(cfgModuleName_[i]), "pdm/pd%u", (unsigned)i);
        snprintf(nvsEnabledKey_[i], sizeof(nvsEnabledKey_[i]), NvsKeys::PoolDevice::EnabledFmt, (unsigned)i);
        snprintf(nvsDependsKey_[i], sizeof(nvsDependsKey_[i]), NvsKeys::PoolDevice::DependsFmt, (unsigned)i);
        snprintf(nvsFlowKey_[i], sizeof(nvsFlowKey_[i]), NvsKeys::PoolDevice::FlowFmt, (unsigned)i);
        snprintf(nvsTankCapKey_[i], sizeof(nvsTankCapKey_[i]), NvsKeys::PoolDevice::TankCapFmt, (unsigned)i);
        snprintf(nvsTankInitKey_[i], sizeof(nvsTankInitKey_[i]), NvsKeys::PoolDevice::TankInitFmt, (unsigned)i);
        snprintf(nvsMaxUptimeKey_[i], sizeof(nvsMaxUptimeKey_[i]), NvsKeys::PoolDevice::MaxUptimeFmt, (unsigned)i);
        snprintf(nvsRuntimeKey_[i], sizeof(nvsRuntimeKey_[i]), NvsKeys::PoolDevice::RuntimeFmt, (unsigned)i);
        snprintf(cfgRuntimeModuleName_[i], sizeof(cfgRuntimeModuleName_[i]), "pdmrt/pd%u", (unsigned)i);

        if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
            MqttConfigRouteProducer::Route& cfgRoute = cfgRoutes_[cfgRouteCount_++];
            cfgRoute = MqttConfigRouteProducer::Route{};
            cfgRoute.messageId = (uint16_t)(kCfgMsgPdmSlotBase + i);
            cfgRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, localBranchId};
            cfgRoute.moduleName = cfgModuleName_[i];
            cfgRoute.topicSuffix = s.id;
            cfgRoute.topicBase = kPoolDeviceCfgTopicBase;
            cfgRoute.changePriority = (uint8_t)MqttPublishPriority::Normal;
        }
        if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
            MqttConfigRouteProducer::Route& cfgRuntimeRoute = cfgRoutes_[cfgRouteCount_++];
            cfgRuntimeRoute = MqttConfigRouteProducer::Route{};
            cfgRuntimeRoute.messageId = (uint16_t)(kCfgMsgPdmrtSlotBase + i);
            cfgRuntimeRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, runtimeBranchId};
            cfgRuntimeRoute.moduleName = cfgRuntimeModuleName_[i];
            cfgRuntimeRoute.topicSuffix = s.id;
            cfgRuntimeRoute.topicBase = kPoolDeviceCfgRuntimeTopicBase;
            cfgRuntimeRoute.changePriority = (uint8_t)MqttPublishPriority::Normal;
        }

        cfgEnabledVar_[i].nvsKey = nvsEnabledKey_[i];
        cfgEnabledVar_[i].jsonName = "enabled";
        cfgEnabledVar_[i].moduleName = cfgModuleName_[i];
        cfgEnabledVar_[i].type = ConfigType::Bool;
        cfgEnabledVar_[i].value = &s.def.enabled;
        cfgEnabledVar_[i].persistence = ConfigPersistence::Persistent;
        cfgEnabledVar_[i].size = 0;
        cfg.registerVar(cfgEnabledVar_[i], kCfgModuleId, localBranchId);

        cfgDependsVar_[i].nvsKey = nvsDependsKey_[i];
        cfgDependsVar_[i].jsonName = "depends_on_mask";
        cfgDependsVar_[i].moduleName = cfgModuleName_[i];
        cfgDependsVar_[i].type = ConfigType::UInt8;
        cfgDependsVar_[i].value = &s.def.dependsOnMask;
        cfgDependsVar_[i].persistence = ConfigPersistence::Persistent;
        cfgDependsVar_[i].size = 0;
        cfg.registerVar(cfgDependsVar_[i], kCfgModuleId, localBranchId);

        cfgFlowVar_[i].nvsKey = nvsFlowKey_[i];
        cfgFlowVar_[i].jsonName = "flow_l_h";
        cfgFlowVar_[i].moduleName = cfgModuleName_[i];
        cfgFlowVar_[i].type = ConfigType::Float;
        cfgFlowVar_[i].value = &s.def.flowLPerHour;
        cfgFlowVar_[i].persistence = ConfigPersistence::Persistent;
        cfgFlowVar_[i].size = 0;
        cfg.registerVar(cfgFlowVar_[i], kCfgModuleId, localBranchId);

        cfgTankCapVar_[i].nvsKey = nvsTankCapKey_[i];
        cfgTankCapVar_[i].jsonName = "tank_cap_ml";
        cfgTankCapVar_[i].moduleName = cfgModuleName_[i];
        cfgTankCapVar_[i].type = ConfigType::Float;
        cfgTankCapVar_[i].value = &s.def.tankCapacityMl;
        cfgTankCapVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankCapVar_[i].size = 0;
        cfg.registerVar(cfgTankCapVar_[i], kCfgModuleId, localBranchId);

        cfgTankInitVar_[i].nvsKey = nvsTankInitKey_[i];
        cfgTankInitVar_[i].jsonName = "tank_init_ml";
        cfgTankInitVar_[i].moduleName = cfgModuleName_[i];
        cfgTankInitVar_[i].type = ConfigType::Float;
        cfgTankInitVar_[i].value = &s.def.tankInitialMl;
        cfgTankInitVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankInitVar_[i].size = 0;
        cfg.registerVar(cfgTankInitVar_[i], kCfgModuleId, localBranchId);

        cfgMaxUptimeVar_[i].nvsKey = nvsMaxUptimeKey_[i];
        cfgMaxUptimeVar_[i].jsonName = "max_uptime_day_s";
        cfgMaxUptimeVar_[i].moduleName = cfgModuleName_[i];
        cfgMaxUptimeVar_[i].type = ConfigType::Int32;
        cfgMaxUptimeVar_[i].value = &s.def.maxUptimeDaySec;
        cfgMaxUptimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgMaxUptimeVar_[i].size = 0;
        cfg.registerVar(cfgMaxUptimeVar_[i], kCfgModuleId, localBranchId);

        cfgRuntimeVar_[i].nvsKey = nvsRuntimeKey_[i];
        cfgRuntimeVar_[i].jsonName = "metrics_blob";
        cfgRuntimeVar_[i].moduleName = cfgRuntimeModuleName_[i];
        cfgRuntimeVar_[i].type = ConfigType::CharArray;
        cfgRuntimeVar_[i].value = runtimePersistBuf_[i];
        cfgRuntimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgRuntimeVar_[i].size = sizeof(runtimePersistBuf_[i]);
        cfg.registerVar(cfgRuntimeVar_[i], kCfgModuleId, runtimeBranchId);
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.write", cmdPoolWrite_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.write", cmdPoolWrite_, this); // backward compatibility
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.refill", cmdPoolRefill_, this);
    }
    if (haSvc_ && haSvc_->addSensor) {
        if (slots_[PoolBinding::kDeviceSlotChlorinePump].used) {
            const HASensorEntry s0{
                "pooldev", "pd_chl_pmp_upt", "Pump uptime Chlorine",
                "rt/pdm/metrics/pd2", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0);
            const HASensorEntry s0b{
                "pooldev", "pd_chl_tnk_rem", "Tank remaining Chlorine",
                "rt/pdm/metrics/pd2", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:water-check", "L"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0b);
        }
        if (slots_[PoolBinding::kDeviceSlotPhPump].used) {
            const HASensorEntry s1{
                "pooldev", "pd_ph_pmp_upt", "Pump uptime pH",
                "rt/pdm/metrics/pd1", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1);
            const HASensorEntry s1b{
                "pooldev", "pd_ph_tnk_rem", "Tank remaining pH",
                "rt/pdm/metrics/pd1", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:beaker-check-outline", "L", false
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1b);
        }
        if (slots_[PoolBinding::kDeviceSlotFillPump].used) {
            const HASensorEntry s2{
                "pooldev", "pd_fill_upt_mn", "Pump uptime Fill",
                "rt/pdm/metrics/pd4", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s2);
        }
        if (slots_[PoolBinding::kDeviceSlotFiltrationPump].used) {
            const HASensorEntry s3{
                "pooldev", "pd_flt_upt_mn", "Pump uptime Filtration",
                "rt/pdm/metrics/pd0", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s3);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorineGenerator].used) {
            const HASensorEntry s4{
                "pooldev", "pd_chl_gen_upt", "Pump uptime Chlorine Generator",
                "rt/pdm/metrics/pd5", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s4);
        }
    }

    if (haSvc_ && haSvc_->addNumber) {
        if (slots_[0].used) {
            const HANumberEntry n0{
                "pooldev", "pd0_flow", "Filtration Pump Flowrate",
                "cfg/pdm/pd0", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd0\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0);
            const HANumberEntry n0b{
                "pooldev", "pd0_max_upt", "Max Uptime Filtration Pump",
                "cfg/pdm/pd0", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd0\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0b);
        }
        if (slots_[1].used) {
            const HANumberEntry n1{
                "pooldev", "pd1_flow", "pH Pump Flowrate",
                "cfg/pdm/pd1", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n1);
        }
        if (slots_[2].used) {
            const HANumberEntry n2{
                "pooldev", "pd2_flow", "Chlorine Pump Flowrate",
                "cfg/pdm/pd2", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n2);
        }
        if (slots_[PoolBinding::kDeviceSlotPhPump].used) {
            const HANumberEntry n3{
                "pooldev", "pd1_max_upt", "Max Uptime pH Pump",
                "cfg/pdm/pd1", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                1.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n3);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorinePump].used) {
            const HANumberEntry n4{
                "pooldev", "pd2_max_upt", "Max Uptime Chlorine Pump",
                "cfg/pdm/pd2", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                1.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n4);
        }
        if (slots_[PoolBinding::kDeviceSlotFillPump].used) {
            const HANumberEntry n4b{
                "pooldev", "pd4_max_upt", "Max Uptime Fill Pump",
                "cfg/pdm/pd4", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd4\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n4b);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorineGenerator].used) {
            const HANumberEntry n5{
                "pooldev", "pd5_max_upt", "Max Uptime Chlorine Generator",
                "cfg/pdm/pd5", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd5\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n5);
        }
    }
    if (haSvc_ && haSvc_->addButton) {
        if (slots_[PoolBinding::kDeviceSlotPhPump].used) {
            const HAButtonEntry refillPhTank{
                "pooldev",
                "pd_refill_ph",
                "Fill pH Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":1}}",
                "config",
                "mdi:beaker-plus-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillPhTank);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorinePump].used) {
            const HAButtonEntry refillChlorineTank{
                "pooldev",
                "pd_refill_chl",
                "Fill Chlorine Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":2}}",
                "config",
                "mdi:water-plus"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillChlorineTank);
        }
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    BufferUsageTracker::note(TrackedBufferId::PoolDeviceSlots,
                             (size_t)count * sizeof(PoolDeviceSlot),
                             sizeof(slots_),
                             "init",
                             nullptr);
    LOGI("PoolDevice module ready (devices=%u)", (unsigned)count);
    (void)logHub_;
}

void PoolDeviceModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_ && cfgRoutes_ && cfgRouteCount_ > 0U) {
        cfgMqttPub_->configure(this,
                               kPoolDeviceCfgProducerId,
                               cfgRoutes_,
                               cfgRouteCount_,
                               services);
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        if (runtimePersistBuf_[i][0] != '\0') {
            BufferUsageTracker::note(TrackedBufferId::PoolDeviceRuntimePersistTable,
                                     charTableUsage_(runtimePersistBuf_),
                                     sizeof(runtimePersistBuf_),
                                     s.id,
                                     runtimePersistBuf_[i]);
        }
        (void)loadPersistedMetrics_(i, s);
    }
    requestPeriodReconcile_();
}

void PoolDeviceModule::loop()
{
    if (!runtimeReady_) {
        if (!configureRuntime_()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            return;
        }
    }

    tickDevices_(millis());
    vTaskDelay(pdMS_TO_TICKS(200));
}
