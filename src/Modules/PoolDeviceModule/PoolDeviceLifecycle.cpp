/**
 * @file PoolDeviceLifecycle.cpp
 * @brief Lifecycle entry points and integration wiring for PoolDeviceModule.
 */

#include "PoolDeviceModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"
#include "Domain/Pool/PoolBindings.h"
#include "Domain/Pool/PoolDeviceSlots.h"
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

    // Each slot owns one config branch and one runtime-persist branch.
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        const uint8_t localBranchId = poolDeviceCfgBranchFromSlot_(i);
        const uint8_t runtimeBranchId = poolDeviceCfgRuntimeBranchFromSlot_(i);

        const PoolDeviceSlotDescriptor& slot = PoolDeviceSlots::kSlots[i];

        cfgEnabledVar_[i].nvsKey = slot.enabledKey;
        cfgEnabledVar_[i].jsonName = "enabled";
        cfgEnabledVar_[i].moduleName = slot.configModuleName;
        cfgEnabledVar_[i].type = ConfigType::Bool;
        cfgEnabledVar_[i].value = &s.def.enabled;
        cfgEnabledVar_[i].persistence = ConfigPersistence::Persistent;
        cfgEnabledVar_[i].size = 0;
        cfg.registerVar(cfgEnabledVar_[i], kCfgModuleId, localBranchId);

        cfgDependsVar_[i].nvsKey = slot.dependsKey;
        cfgDependsVar_[i].jsonName = "depends_on_mask";
        cfgDependsVar_[i].moduleName = slot.configModuleName;
        cfgDependsVar_[i].type = ConfigType::UInt8;
        cfgDependsVar_[i].value = &s.def.dependsOnMask;
        cfgDependsVar_[i].persistence = ConfigPersistence::Persistent;
        cfgDependsVar_[i].size = 0;
        cfg.registerVar(cfgDependsVar_[i], kCfgModuleId, localBranchId);

        cfgFlowVar_[i].nvsKey = slot.flowKey;
        cfgFlowVar_[i].jsonName = "flow_l_h";
        cfgFlowVar_[i].moduleName = slot.configModuleName;
        cfgFlowVar_[i].type = ConfigType::Float;
        cfgFlowVar_[i].value = &s.def.flowLPerHour;
        cfgFlowVar_[i].persistence = ConfigPersistence::Persistent;
        cfgFlowVar_[i].size = 0;
        cfg.registerVar(cfgFlowVar_[i], kCfgModuleId, localBranchId);

        cfgTankCapVar_[i].nvsKey = slot.tankCapKey;
        cfgTankCapVar_[i].jsonName = "tank_cap_ml";
        cfgTankCapVar_[i].moduleName = slot.configModuleName;
        cfgTankCapVar_[i].type = ConfigType::Float;
        cfgTankCapVar_[i].value = &s.def.tankCapacityMl;
        cfgTankCapVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankCapVar_[i].size = 0;
        cfg.registerVar(cfgTankCapVar_[i], kCfgModuleId, localBranchId);

        cfgTankInitVar_[i].nvsKey = slot.tankInitKey;
        cfgTankInitVar_[i].jsonName = "tank_init_ml";
        cfgTankInitVar_[i].moduleName = slot.configModuleName;
        cfgTankInitVar_[i].type = ConfigType::Float;
        cfgTankInitVar_[i].value = &s.def.tankInitialMl;
        cfgTankInitVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankInitVar_[i].size = 0;
        cfg.registerVar(cfgTankInitVar_[i], kCfgModuleId, localBranchId);

        cfgMaxUptimeVar_[i].nvsKey = slot.maxUptimeKey;
        cfgMaxUptimeVar_[i].jsonName = "max_uptime_day_s";
        cfgMaxUptimeVar_[i].moduleName = slot.configModuleName;
        cfgMaxUptimeVar_[i].type = ConfigType::Int32;
        cfgMaxUptimeVar_[i].value = &s.def.maxUptimeDaySec;
        cfgMaxUptimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgMaxUptimeVar_[i].size = 0;
        cfg.registerVar(cfgMaxUptimeVar_[i], kCfgModuleId, localBranchId);

        cfgRuntimeVar_[i].nvsKey = slot.runtimeKey;
        cfgRuntimeVar_[i].jsonName = "metrics_blob";
        cfgRuntimeVar_[i].moduleName = slot.runtimeModuleName;
        cfgRuntimeVar_[i].type = ConfigType::CharArray;
        cfgRuntimeVar_[i].value = runtimePersistBuf_[i];
        cfgRuntimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgRuntimeVar_[i].size = sizeof(runtimePersistBuf_[i]);
        cfg.registerVar(cfgRuntimeVar_[i], kCfgModuleId, runtimeBranchId);
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.write", cmdPoolWrite_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.refill", cmdPoolRefill_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.uptime.reset", cmdPoolResetUptime_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.uptime.reset_all", cmdPoolResetUptimeAll_, this);
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
        if (slots_[PoolBinding::kDeviceSlotFiltrationPump].used) {
            const HAButtonEntry resetFiltrationUptime{
                "pooldev",
                "pd_reset_upt_flt",
                "Reset Uptime Filtration Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":0}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetFiltrationUptime);
        }
        if (slots_[PoolBinding::kDeviceSlotPhPump].used) {
            const HAButtonEntry resetPhUptime{
                "pooldev",
                "pd_reset_upt_ph",
                "Reset Uptime pH Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":1}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetPhUptime);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorinePump].used) {
            const HAButtonEntry resetChlorineUptime{
                "pooldev",
                "pd_reset_upt_chl",
                "Reset Uptime Chlorine Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":2}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetChlorineUptime);
        }
        if (slots_[PoolBinding::kDeviceSlotFillPump].used) {
            const HAButtonEntry resetFillUptime{
                "pooldev",
                "pd_reset_upt_fill",
                "Reset Uptime Fill Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":4}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetFillUptime);
        }
        if (slots_[PoolBinding::kDeviceSlotChlorineGenerator].used) {
            const HAButtonEntry resetGeneratorUptime{
                "pooldev",
                "pd_reset_upt_chl_gen",
                "Reset Uptime Chlorine Generator",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":5}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetGeneratorUptime);
        }
        const HAButtonEntry resetAllUptime{
            "pooldev",
            "pd_reset_upt_all",
            "Reset Uptime All Pool Devices",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"pool.uptime.reset_all\\\"}",
            "diagnostic",
            "mdi:timer-refresh-outline"
        };
        (void)haSvc_->addButton(haSvc_->ctx, &resetAllUptime);
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
    static constexpr MqttConfigRouteProducer::Route kPoolDeviceCfgRoutes[] = {
        {kCfgMsgBasePdm,
         {(uint8_t)ConfigModuleId::PoolDevice, ConfigBranchRef::UnknownLocalBranch},
         nullptr,
         "",
         (uint8_t)MqttPublishPriority::Low,
         &PoolDeviceModule::buildCfgBasePdmStatic_,
         kPoolDeviceCfgTopicBase},
        {kCfgMsgBasePdmrt,
         {(uint8_t)ConfigModuleId::PoolDevice, ConfigBranchRef::UnknownLocalBranch},
         nullptr,
         "",
         (uint8_t)MqttPublishPriority::Low,
         &PoolDeviceModule::buildCfgBasePdmrtStatic_,
         kPoolDeviceCfgRuntimeTopicBase},
#define FLOW_POOLDEVICE_CFG_ROUTES(SLOT) \
        {(uint16_t)(kCfgMsgPdmSlotBase + (SLOT)), \
         {(uint8_t)ConfigModuleId::PoolDevice, poolDeviceCfgBranchFromSlot_(SLOT)}, \
         PoolDeviceSlots::kSlots[SLOT].configModuleName, \
         PoolDeviceSlots::kSlots[SLOT].id, \
         (uint8_t)MqttPublishPriority::Normal, \
         nullptr, \
         kPoolDeviceCfgTopicBase}, \
        {(uint16_t)(kCfgMsgPdmrtSlotBase + (SLOT)), \
         {(uint8_t)ConfigModuleId::PoolDevice, poolDeviceCfgRuntimeBranchFromSlot_(SLOT)}, \
         PoolDeviceSlots::kSlots[SLOT].runtimeModuleName, \
         PoolDeviceSlots::kSlots[SLOT].id, \
         (uint8_t)MqttPublishPriority::Normal, \
         nullptr, \
         kPoolDeviceCfgRuntimeTopicBase}
        FLOW_POOLDEVICE_CFG_ROUTES(0),
        FLOW_POOLDEVICE_CFG_ROUTES(1),
        FLOW_POOLDEVICE_CFG_ROUTES(2),
        FLOW_POOLDEVICE_CFG_ROUTES(3),
        FLOW_POOLDEVICE_CFG_ROUTES(4),
        FLOW_POOLDEVICE_CFG_ROUTES(5),
        FLOW_POOLDEVICE_CFG_ROUTES(6),
        FLOW_POOLDEVICE_CFG_ROUTES(7),
#undef FLOW_POOLDEVICE_CFG_ROUTES
    };
    static_assert((sizeof(kPoolDeviceCfgRoutes) / sizeof(kPoolDeviceCfgRoutes[0])) <= MqttConfigRouteProducer::MaxRoutes,
                  "PoolDevice MQTT config route table exceeds producer capacity");

    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kPoolDeviceCfgProducerId,
                               kPoolDeviceCfgRoutes,
                               (uint8_t)(sizeof(kPoolDeviceCfgRoutes) / sizeof(kPoolDeviceCfgRoutes[0])),
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
