#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModule.h"

#include "Core/DataKeys.h"
#include "Core/Services/Services.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerTypes.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaBoilerModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr const char* kRegModuleNames[kMicronovaRegisterCount] = {
    "micronova/registers/stove_state",
    "micronova/registers/room_temperature",
    "micronova/registers/fumes_temperature",
    "micronova/registers/power_level",
    "micronova/registers/fan_speed",
    "micronova/registers/target_temperature",
    "micronova/registers/water_temperature",
    "micronova/registers/water_pressure",
    "micronova/registers/alarm_code",
};

static constexpr const char* kReadNvs[kMicronovaRegisterCount] = {
    "mnr0r", "mnr1r", "mnr2r", "mnr3r", "mnr4r", "mnr5r", "mnr6r", "mnr7r", "mnr8r"
};
static constexpr const char* kWriteNvs[kMicronovaRegisterCount] = {
    "mnr0w", "mnr1w", "mnr2w", "mnr3w", "mnr4w", "mnr5w", "mnr6w", "mnr7w", "mnr8w"
};
static constexpr const char* kAddressNvs[kMicronovaRegisterCount] = {
    "mnr0a", "mnr1a", "mnr2a", "mnr3a", "mnr4a", "mnr5a", "mnr6a", "mnr7a", "mnr8a"
};
static constexpr const char* kScaleNvs[kMicronovaRegisterCount] = {
    "mnr0s", "mnr1s", "mnr2s", "mnr3s", "mnr4s", "mnr5s", "mnr6s", "mnr7s", "mnr8s"
};
static constexpr const char* kEnabledNvs[kMicronovaRegisterCount] = {
    "mnr0e", "mnr1e", "mnr2e", "mnr3e", "mnr4e", "mnr5e", "mnr6e", "mnr7e", "mnr8e"
};

static bool floatSame(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void copyText(char* dst, size_t len, const char* src)
{
    if (!dst || len == 0U) return;
    if (!src) src = "";
    snprintf(dst, len, "%s", src);
}

static bool shouldSendIrToggle_(bool requestOn, int32_t stoveStateCode)
{
    // Mirror the proven legacy behavior:
    // - ON only when stove is OFF/standby/final-cleaning/alarm-like states.
    // - OFF only when stove is in active run/startup phases.
    if (stoveStateCode < 0) return true; // unknown -> allow command
    if (requestOn) return stoveStateCode == 0 || stoveStateCode > 5;
    return stoveStateCode > 0 && stoveStateCode < 6;
}

static constexpr uint8_t kReadAllSweepStartAddress = 0x00;
static constexpr uint16_t kReadAllSweepAddressCount = 256;
static constexpr uint16_t kReadAllSweepReplyTimeoutMs = 260;
static constexpr uint16_t kReadAllSweepInterAddressDelayMs = MicronovaProtocol::DefaultTurnaroundDelayMs;
static constexpr uint32_t kPollingIntervalRunningMs = 20000UL;
static constexpr uint32_t kPollingIntervalStoppedMs = 60000UL;

static constexpr uint8_t kDisplayLineLen = 7U;
static constexpr uint8_t kDisplayLine1StartAddr = 0x93U;
static constexpr uint8_t kDisplayLine2StartAddr = 0x9BU;
static constexpr uint8_t kDisplayLine3StartAddr = 0xA3U;

static bool isStoveRunningState_(int32_t stoveStateCode)
{
    return stoveStateCode > 0 && stoveStateCode < 6;
}

static char printableAscii_(uint8_t value)
{
    if (value >= 0x20U && value <= 0x7EU) return (char)value;
    return ' ';
}
}

MicronovaBoilerModule::MicronovaBoilerModule()
    : normalIntervalVar_{NVS_KEY("mn_pnorm"), "normal_interval_ms", "micronova/poll", ConfigType::Int32, &normalIntervalMs_, ConfigPersistence::Persistent, 0},
      fastIntervalVar_{NVS_KEY("mn_pfast"), "fast_interval_ms", "micronova/poll", ConfigType::Int32, &fastIntervalMs_, ConfigPersistence::Persistent, 0},
      fastCyclesVar_{NVS_KEY("mn_pcyc"), "fast_cycles", "micronova/poll", ConfigType::Int32, &fastCyclesCfg_, ConfigPersistence::Persistent, 0},
      powerOnWriteVar_{NVS_KEY("mn_on_wc"), "write_code", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.writeCode, ConfigPersistence::Persistent, 0},
      powerOnAddressVar_{NVS_KEY("mn_on_ad"), "address", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.address, ConfigPersistence::Persistent, 0},
      powerOnValueVar_{NVS_KEY("mn_on_va"), "value", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.value, ConfigPersistence::Persistent, 0},
      powerOnRepeatCountVar_{NVS_KEY("mn_on_rc"), "repeat_count", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.repeatCount, ConfigPersistence::Persistent, 0},
      powerOnRepeatDelayVar_{NVS_KEY("mn_on_rd"), "repeat_delay_ms", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.repeatDelayMs, ConfigPersistence::Persistent, 0},
      powerOffWriteVar_{NVS_KEY("mn_off_wc"), "write_code", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.writeCode, ConfigPersistence::Persistent, 0},
      powerOffAddressVar_{NVS_KEY("mn_off_ad"), "address", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.address, ConfigPersistence::Persistent, 0},
      powerOffValueVar_{NVS_KEY("mn_off_va"), "value", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.value, ConfigPersistence::Persistent, 0},
      powerOffRepeatCountVar_{NVS_KEY("mn_off_rc"), "repeat_count", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.repeatCount, ConfigPersistence::Persistent, 0},
      powerOffRepeatDelayVar_{NVS_KEY("mn_off_rd"), "repeat_delay_ms", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.repeatDelayMs, ConfigPersistence::Persistent, 0},
      powerPlusWriteVar_{NVS_KEY("mn_pp_wc"), "write_code", "micronova/registers/power_plus", ConfigType::Int32, &powerPlus_.writeCode, ConfigPersistence::Persistent, 0},
      powerPlusAddressVar_{NVS_KEY("mn_pp_ad"), "address", "micronova/registers/power_plus", ConfigType::Int32, &powerPlus_.address, ConfigPersistence::Persistent, 0},
      powerPlusValueVar_{NVS_KEY("mn_pp_va"), "value", "micronova/registers/power_plus", ConfigType::Int32, &powerPlus_.value, ConfigPersistence::Persistent, 0},
      powerPlusRepeatCountVar_{NVS_KEY("mn_pp_rc"), "repeat_count", "micronova/registers/power_plus", ConfigType::Int32, &powerPlus_.repeatCount, ConfigPersistence::Persistent, 0},
      powerPlusRepeatDelayVar_{NVS_KEY("mn_pp_rd"), "repeat_delay_ms", "micronova/registers/power_plus", ConfigType::Int32, &powerPlus_.repeatDelayMs, ConfigPersistence::Persistent, 0},
      powerMinusWriteVar_{NVS_KEY("mn_pm_wc"), "write_code", "micronova/registers/power_minus", ConfigType::Int32, &powerMinus_.writeCode, ConfigPersistence::Persistent, 0},
      powerMinusAddressVar_{NVS_KEY("mn_pm_ad"), "address", "micronova/registers/power_minus", ConfigType::Int32, &powerMinus_.address, ConfigPersistence::Persistent, 0},
      powerMinusValueVar_{NVS_KEY("mn_pm_va"), "value", "micronova/registers/power_minus", ConfigType::Int32, &powerMinus_.value, ConfigPersistence::Persistent, 0},
      powerMinusRepeatCountVar_{NVS_KEY("mn_pm_rc"), "repeat_count", "micronova/registers/power_minus", ConfigType::Int32, &powerMinus_.repeatCount, ConfigPersistence::Persistent, 0},
      powerMinusRepeatDelayVar_{NVS_KEY("mn_pm_rd"), "repeat_delay_ms", "micronova/registers/power_minus", ConfigType::Int32, &powerMinus_.repeatDelayMs, ConfigPersistence::Persistent, 0}
{
    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const MicronovaRegisterDef& def = kMicronovaDefaultRegisters[i];
        regs_[i].readCode = def.readCode;
        regs_[i].writeCode = def.writeCode;
        regs_[i].address = def.address;
        regs_[i].scale = def.scale;
        regs_[i].offset = def.offset;
        regs_[i].writable = def.writable;
        regs_[i].enabled = def.enabled;

        regReadVars_[i] = {kReadNvs[i], "read_code", kRegModuleNames[i], ConfigType::Int32, &regs_[i].readCode, ConfigPersistence::Persistent, 0};
        regWriteVars_[i] = {kWriteNvs[i], "write_code", kRegModuleNames[i], ConfigType::Int32, &regs_[i].writeCode, ConfigPersistence::Persistent, 0};
        regAddressVars_[i] = {kAddressNvs[i], "address", kRegModuleNames[i], ConfigType::Int32, &regs_[i].address, ConfigPersistence::Persistent, 0};
        regScaleVars_[i] = {kScaleNvs[i], "scale", kRegModuleNames[i], ConfigType::Float, &regs_[i].scale, ConfigPersistence::Persistent, 0};
        regEnabledVars_[i] = {kEnabledNvs[i], "enabled", kRegModuleNames[i], ConfigType::Bool, &regs_[i].enabled, ConfigPersistence::Persistent, 0};
    }

    powerOn_.writeCode = kMicronovaPowerOnDefault.writeCode;
    powerOn_.address = kMicronovaPowerOnDefault.address;
    powerOn_.value = kMicronovaPowerOnDefault.value;
    powerOn_.repeatCount = kMicronovaPowerOnDefault.repeatCount;
    powerOn_.repeatDelayMs = kMicronovaPowerOnDefault.repeatDelayMs;

    powerOff_.writeCode = kMicronovaPowerOffDefault.writeCode;
    powerOff_.address = kMicronovaPowerOffDefault.address;
    powerOff_.value = kMicronovaPowerOffDefault.value;
    powerOff_.repeatCount = kMicronovaPowerOffDefault.repeatCount;
    powerOff_.repeatDelayMs = kMicronovaPowerOffDefault.repeatDelayMs;

    powerPlus_.writeCode = kMicronovaPowerPlusDefault.writeCode;
    powerPlus_.address = kMicronovaPowerPlusDefault.address;
    powerPlus_.value = kMicronovaPowerPlusDefault.value;
    powerPlus_.repeatCount = kMicronovaPowerPlusDefault.repeatCount;
    powerPlus_.repeatDelayMs = kMicronovaPowerPlusDefault.repeatDelayMs;

    powerMinus_.writeCode = kMicronovaPowerMinusDefault.writeCode;
    powerMinus_.address = kMicronovaPowerMinusDefault.address;
    powerMinus_.value = kMicronovaPowerMinusDefault.value;
    powerMinus_.repeatCount = kMicronovaPowerMinusDefault.repeatCount;
    powerMinus_.repeatDelayMs = kMicronovaPowerMinusDefault.repeatDelayMs;
}

void MicronovaBoilerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Micronova;
    cfg.registerVar(normalIntervalVar_, kCfgModuleId, 20);
    cfg.registerVar(fastIntervalVar_, kCfgModuleId, 20);
    cfg.registerVar(fastCyclesVar_, kCfgModuleId, 20);

    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const uint8_t branch = (uint8_t)(30U + i);
        cfg.registerVar(regReadVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regWriteVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regAddressVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regScaleVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regEnabledVars_[i], kCfgModuleId, branch);
    }

    cfg.registerVar(powerOnWriteVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnAddressVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnValueVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnRepeatCountVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnRepeatDelayVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOffWriteVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffAddressVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffValueVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffRepeatCountVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffRepeatDelayVar_, kCfgModuleId, 51);
    cfg.registerVar(powerPlusWriteVar_, kCfgModuleId, 52);
    cfg.registerVar(powerPlusAddressVar_, kCfgModuleId, 52);
    cfg.registerVar(powerPlusValueVar_, kCfgModuleId, 52);
    cfg.registerVar(powerPlusRepeatCountVar_, kCfgModuleId, 52);
    cfg.registerVar(powerPlusRepeatDelayVar_, kCfgModuleId, 52);
    cfg.registerVar(powerMinusWriteVar_, kCfgModuleId, 53);
    cfg.registerVar(powerMinusAddressVar_, kCfgModuleId, 53);
    cfg.registerVar(powerMinusValueVar_, kCfgModuleId, 53);
    cfg.registerVar(powerMinusRepeatCountVar_, kCfgModuleId, 53);
    cfg.registerVar(powerMinusRepeatDelayVar_, kCfgModuleId, 53);

    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    if (eventBus_) {
        eventBus_->subscribe(EventId::MicronovaCommandPower, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandPowerLevel, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandFanSpeed, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandTargetTemperature, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandRefresh, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandRawWrite, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandSweepReadAll, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandPowerPlus, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandPowerMinus, &MicronovaBoilerModule::onEventStatic_, this);
    }
}

void MicronovaBoilerModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    // This Micronova target has no fan-speed register.
    // Force-disable it even if old persisted config exists.
    reg_(MicronovaRegisterId::FanSpeed).enabled = false;
    reg_(MicronovaRegisterId::FanSpeed).writable = false;
    LOGI("Micronova boiler begin deferred");
}

void MicronovaBoilerModule::onStart(ConfigStore&, ServiceRegistry&)
{
    (void)begin();
}

bool MicronovaBoilerModule::begin()
{
    begun_ = bus_ != nullptr;
    nextPollMs_ = millis();
    return begun_;
}

void MicronovaBoilerModule::loop()
{
    tick(millis());
}

const MicronovaBoilerModule::RegisterConfig& MicronovaBoilerModule::reg_(MicronovaRegisterId id) const
{
    return regs_[(uint8_t)id];
}

MicronovaBoilerModule::RegisterConfig& MicronovaBoilerModule::reg_(MicronovaRegisterId id)
{
    return regs_[(uint8_t)id];
}

uint8_t MicronovaBoilerModule::clampLevel_(uint8_t level) const
{
    return level > 5U ? 5U : level;
}

bool MicronovaBoilerModule::queueRegisterRead_(MicronovaRegisterId id)
{
    if (!bus_) return false;
    const RegisterConfig& r = reg_(id);
    if (!r.enabled || r.address < 0) return false;
    return bus_->queueRead((uint8_t)r.readCode, (uint8_t)r.address);
}

void MicronovaBoilerModule::queuePollingSweep_(uint32_t nowMs)
{
    if (!bus_) return;
    if (bus_->hasPendingCommands()) {
        nextPollMs_ = nowMs + 500U;
        return;
    }

    (void)queueRegisterRead_(MicronovaRegisterId::StoveState);
    (void)queueRegisterRead_(MicronovaRegisterId::RoomTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::TargetTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::FumesTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::PowerLevel);
    (void)queueRegisterRead_(MicronovaRegisterId::FanSpeed);
    (void)queueRegisterRead_(MicronovaRegisterId::WaterTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::WaterPressure);
    (void)queueRegisterRead_(MicronovaRegisterId::AlarmCode);
    queueDisplayLineReads_();

    const uint32_t runningInterval = (fastIntervalMs_ > 0) ? (uint32_t)fastIntervalMs_ : kPollingIntervalRunningMs;
    const uint32_t stoppedInterval = (normalIntervalMs_ > 0) ? (uint32_t)normalIntervalMs_ : kPollingIntervalStoppedMs;
    const bool running = dataStore_ ? isStoveRunningState_(dataStore_->data().micronova.stoveStateCode) : false;
    const uint32_t interval = running ? runningInterval : stoppedInterval;
    nextPollMs_ = nowMs + interval;
}

void MicronovaBoilerModule::queueDisplayLineReads_()
{
    if (!bus_) return;
    for (uint8_t i = 0; i < kDisplayLineLen; ++i) {
        (void)bus_->queueRead(MicronovaProtocol::RamRead, (uint8_t)(kDisplayLine1StartAddr + i));
    }
    for (uint8_t i = 0; i < kDisplayLineLen; ++i) {
        (void)bus_->queueRead(MicronovaProtocol::RamRead, (uint8_t)(kDisplayLine2StartAddr + i));
    }
    for (uint8_t i = 0; i < kDisplayLineLen; ++i) {
        (void)bus_->queueRead(MicronovaProtocol::RamRead, (uint8_t)(kDisplayLine3StartAddr + i));
    }
}

void MicronovaBoilerModule::requestReadAllSweep_()
{
    if (readAllSweepActive_ || readAllSweepRequested_) {
        LOGW("Micronova read-all sweep already active/requested");
        return;
    }
    readAllSweepRequested_ = true;
    readAllSweepNextStepMs_ = millis();
    LOGI("Micronova read-all sweep requested");
}

void MicronovaBoilerModule::startReadAllSweep_(uint32_t nowMs)
{
    if (!bus_) return;
    if (bus_->hasPendingCommands()) return;

    bus_->clearAllQueues();

    readAllSweepRequested_ = false;
    readAllSweepActive_ = true;
    readAllSweepAwaitingReply_ = false;
    readAllSweepAddress_ = kReadAllSweepStartAddress;
    readAllSweepIndex_ = 0U;
    readAllSweepSentMs_ = 0U;
    readAllSweepNextStepMs_ = nowMs;
    readAllSweepValidCount_ = 0U;
    readAllSweepInvalidCount_ = 0U;
    readAllSweepTimeoutCount_ = 0U;
    LOGI("Micronova read-all sweep start range=0x00..0xFF timeout_ms=%u inter_delay_ms=%u",
         (unsigned)kReadAllSweepReplyTimeoutMs,
         (unsigned)kReadAllSweepInterAddressDelayMs);
}

void MicronovaBoilerModule::finishReadAllSweep_(bool aborted)
{
    LOGI("Micronova read-all sweep %s valid=%u invalid=%u timeout=%u total=%u",
         aborted ? "aborted" : "done",
         (unsigned)readAllSweepValidCount_,
         (unsigned)readAllSweepInvalidCount_,
         (unsigned)readAllSweepTimeoutCount_,
         (unsigned)(readAllSweepValidCount_ + readAllSweepInvalidCount_ + readAllSweepTimeoutCount_));
    readAllSweepRequested_ = false;
    readAllSweepActive_ = false;
    readAllSweepAwaitingReply_ = false;
    readAllSweepAddress_ = kReadAllSweepStartAddress;
    readAllSweepIndex_ = 0U;
    readAllSweepSentMs_ = 0U;
    readAllSweepNextStepMs_ = millis();
}

void MicronovaBoilerModule::onReadAllSweepReply_(const MicronovaRawValue& value, uint32_t nowMs)
{
    if (!readAllSweepActive_ || !readAllSweepAwaitingReply_) return;
    if (value.memoryAddress != readAllSweepAddress_) return;

    if (value.valid) {
        ++readAllSweepValidCount_;
        LOGI("Micronova sweep addr=0x%02X code=0x%02X value=0x%02X",
             (unsigned)value.memoryAddress,
             (unsigned)value.readCode,
             (unsigned)((uint8_t)value.value));
    } else {
        ++readAllSweepInvalidCount_;
        LOGW("Micronova sweep addr=0x%02X invalid reply",
             (unsigned)value.memoryAddress);
    }

    readAllSweepAwaitingReply_ = false;
    readAllSweepAddress_ = (uint8_t)(readAllSweepAddress_ + 1U);
    ++readAllSweepIndex_;
    readAllSweepNextStepMs_ = nowMs + (uint32_t)kReadAllSweepInterAddressDelayMs;
}

void MicronovaBoilerModule::tickReadAllSweep_(uint32_t nowMs)
{
    if (!bus_) return;

    if (!readAllSweepActive_) {
        if (!readAllSweepRequested_) return;
        if ((int32_t)(nowMs - readAllSweepNextStepMs_) < 0) return;
        startReadAllSweep_(nowMs);
        return;
    }

    if (readAllSweepAwaitingReply_) {
        if (bus_->isIdle() && (uint32_t)(nowMs - readAllSweepSentMs_) >= (uint32_t)kReadAllSweepReplyTimeoutMs) {
            ++readAllSweepTimeoutCount_;
            LOGW("Micronova sweep addr=0x%02X timeout",
                 (unsigned)readAllSweepAddress_);
            readAllSweepAwaitingReply_ = false;
            readAllSweepAddress_ = (uint8_t)(readAllSweepAddress_ + 1U);
            ++readAllSweepIndex_;
            readAllSweepNextStepMs_ = nowMs + (uint32_t)kReadAllSweepInterAddressDelayMs;
        }
        return;
    }

    if (readAllSweepIndex_ >= kReadAllSweepAddressCount) {
        finishReadAllSweep_(false);
        return;
    }

    if ((int32_t)(nowMs - readAllSweepNextStepMs_) < 0) return;
    if (bus_->hasPendingCommands()) return;

    const bool queued = bus_->queueRead(MicronovaProtocol::RamRead, readAllSweepAddress_);
    if (!queued) return;

    readAllSweepAwaitingReply_ = true;
    readAllSweepSentMs_ = nowMs;
    LOGD("Micronova sweep tx read addr=0x%02X",
         (unsigned)readAllSweepAddress_);
}

void MicronovaBoilerModule::syncOnline_(uint32_t)
{
    if (!bus_ || !dataStore_) return;
    const bool online = bus_->isOnline();
    if (online == lastOnline_) return;
    lastOnline_ = online;
    RuntimeData& rt = dataStore_->dataMutable();
    if (rt.micronova.online != online) {
        rt.micronova.online = online;
        dataStore_->notifyChanged(DataKeys::MicronovaOnline);
    }
    if (eventBus_) {
        eventBus_->post(EventId::MicronovaOnlineChanged, nullptr, 0, moduleId());
    }
}

void MicronovaBoilerModule::tick(uint32_t nowMs)
{
    if (!begun_ || !bus_) return;
    syncOnline_(nowMs);

    MicronovaRawValue raw{};
    while (bus_->pollValue(raw)) {
        if (readAllSweepActive_ && readAllSweepAwaitingReply_) {
            onReadAllSweepReply_(raw, nowMs);
        }
        handleRawValue_(raw);
    }

    if (readAllSweepRequested_ || readAllSweepActive_) {
        tickReadAllSweep_(nowMs);
        return;
    }

    if ((int32_t)(nowMs - nextPollMs_) >= 0) {
        queuePollingSweep_(nowMs);
    }
}

void MicronovaBoilerModule::handleRawValue_(const MicronovaRawValue& value)
{
    if (!value.valid) return;
    if (updateDisplayLinesFromRaw_(value, millis())) return;
    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const RegisterConfig& r = regs_[i];
        if (!r.enabled) continue;
        if ((uint8_t)r.readCode != value.readCode || (uint8_t)r.address != value.memoryAddress) continue;
        const float converted = ((float)value.value * r.scale) + r.offset;
        publishRuntimeValue_((MicronovaRegisterId)i, converted, value.value, millis());
        return;
    }
}

bool MicronovaBoilerModule::updateDisplayLinesFromRaw_(const MicronovaRawValue& value, uint32_t nowMs)
{
    if (!dataStore_) return false;
    if (value.readCode != MicronovaProtocol::RamRead) return false;

    RuntimeData& rt = dataStore_->dataMutable();
    DataKey lineKey = 0;
    char* targetLine = nullptr;
    uint8_t lineStart = 0U;
    if (value.memoryAddress >= kDisplayLine1StartAddr && value.memoryAddress < (uint8_t)(kDisplayLine1StartAddr + kDisplayLineLen)) {
        lineKey = DataKeys::MicronovaDisplayLine1;
        targetLine = rt.micronova.displayLine1;
        lineStart = kDisplayLine1StartAddr;
    } else if (value.memoryAddress >= kDisplayLine2StartAddr && value.memoryAddress < (uint8_t)(kDisplayLine2StartAddr + kDisplayLineLen)) {
        lineKey = DataKeys::MicronovaDisplayLine2;
        targetLine = rt.micronova.displayLine2;
        lineStart = kDisplayLine2StartAddr;
    } else if (value.memoryAddress >= kDisplayLine3StartAddr && value.memoryAddress < (uint8_t)(kDisplayLine3StartAddr + kDisplayLineLen)) {
        lineKey = DataKeys::MicronovaDisplayLine3;
        targetLine = rt.micronova.displayLine3;
        lineStart = kDisplayLine3StartAddr;
    } else {
        return false;
    }

    const uint8_t idx = (uint8_t)(value.memoryAddress - lineStart);
    const char newChar = printableAscii_((uint8_t)value.value);
    if (idx < kDisplayLineLen && targetLine[idx] != newChar) {
        targetLine[idx] = newChar;
        targetLine[kDisplayLineLen] = '\0';
        const uint8_t dirtyBit = (lineKey == DataKeys::MicronovaDisplayLine1) ? 0x01U
                               : (lineKey == DataKeys::MicronovaDisplayLine2) ? 0x02U
                               : 0x04U;
        displayLineDirtyMask_ |= dirtyBit;
    }

    if (idx == (kDisplayLineLen - 1U)) {
        const uint8_t dirtyBit = (lineKey == DataKeys::MicronovaDisplayLine1) ? 0x01U
                               : (lineKey == DataKeys::MicronovaDisplayLine2) ? 0x02U
                               : 0x04U;
        if ((displayLineDirtyMask_ & dirtyBit) != 0U) {
            dataStore_->notifyChanged(lineKey);
            displayLineDirtyMask_ &= (uint8_t)(~dirtyBit);
            const uint8_t lineNo = (lineKey == DataKeys::MicronovaDisplayLine1) ? 1U
                                 : (lineKey == DataKeys::MicronovaDisplayLine2) ? 2U
                                 : 3U;
            LOGI("Micronova display line%u='%s'", (unsigned)lineNo, targetLine);
        }
    }
    if (rt.micronova.lastUpdateMs != nowMs) {
        rt.micronova.lastUpdateMs = nowMs;
        dataStore_->notifyChanged(DataKeys::MicronovaLastUpdateMs);
    }
    return true;
}

void MicronovaBoilerModule::publishRuntimeValue_(MicronovaRegisterId id, float converted, int16_t raw, uint32_t nowMs)
{
    if (!dataStore_) return;

    RuntimeData& rt = dataStore_->dataMutable();
    bool changed = false;
    DataKey key = DataKeys::MicronovaLastUpdateMs;

    switch (id) {
        case MicronovaRegisterId::StoveState: {
            const int32_t code = (int32_t)raw;
            const char* stateText = micronovaStoveStateText((uint8_t)code);
            const char* powerText = micronovaPowerStateText(micronovaPowerStateFromStoveState((uint8_t)code));
            if (rt.micronova.stoveStateCode != code) {
                rt.micronova.stoveStateCode = code;
                changed = true;
                key = DataKeys::MicronovaStoveStateCode;
                dataStore_->notifyChanged(DataKeys::MicronovaStoveStateCode);
            }
            if (strncmp(rt.micronova.stoveStateText, stateText, sizeof(rt.micronova.stoveStateText)) != 0) {
                copyText(rt.micronova.stoveStateText, sizeof(rt.micronova.stoveStateText), stateText);
                dataStore_->notifyChanged(DataKeys::MicronovaStoveStateText);
                changed = true;
            }
            if (strncmp(rt.micronova.powerState, powerText, sizeof(rt.micronova.powerState)) != 0) {
                copyText(rt.micronova.powerState, sizeof(rt.micronova.powerState), powerText);
                dataStore_->notifyChanged(DataKeys::MicronovaPowerState);
                changed = true;
            }
            break;
        }
        case MicronovaRegisterId::RoomTemperature:
            if (!floatSame(rt.micronova.roomTemperature, converted)) {
                rt.micronova.roomTemperature = converted;
                key = DataKeys::MicronovaRoomTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::FumesTemperature:
            if (!floatSame(rt.micronova.fumesTemperature, converted)) {
                rt.micronova.fumesTemperature = converted;
                key = DataKeys::MicronovaFumesTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::PowerLevel:
            if (rt.micronova.powerLevel != (int32_t)raw) {
                rt.micronova.powerLevel = raw;
                key = DataKeys::MicronovaPowerLevel;
                changed = true;
            }
            break;
        case MicronovaRegisterId::FanSpeed:
            if (rt.micronova.fanSpeed != (int32_t)raw) {
                rt.micronova.fanSpeed = raw;
                key = DataKeys::MicronovaFanSpeed;
                changed = true;
            }
            break;
        case MicronovaRegisterId::TargetTemperature:
            if (rt.micronova.targetTemperature != (int32_t)lroundf(converted)) {
                rt.micronova.targetTemperature = (int32_t)lroundf(converted);
                key = DataKeys::MicronovaTargetTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::WaterTemperature:
            if (!floatSame(rt.micronova.waterTemperature, converted)) {
                rt.micronova.waterTemperature = converted;
                key = DataKeys::MicronovaWaterTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::WaterPressure:
            if (!floatSame(rt.micronova.waterPressure, converted)) {
                rt.micronova.waterPressure = converted;
                key = DataKeys::MicronovaWaterPressure;
                changed = true;
            }
            break;
        case MicronovaRegisterId::AlarmCode:
            if (rt.micronova.alarmCode != (int32_t)raw) {
                rt.micronova.alarmCode = raw;
                key = DataKeys::MicronovaAlarmCode;
                changed = true;
            }
            break;
        case MicronovaRegisterId::Count:
            break;
    }

    if (changed) {
        if (key != DataKeys::MicronovaStoveStateCode) {
            dataStore_->notifyChanged(key);
        }
        MicronovaValueUpdatedPayload payload{};
        snprintf(payload.key, sizeof(payload.key), "%s", kMicronovaDefaultRegisters[(uint8_t)id].key);
        payload.value = converted;
        payload.raw = raw;
        if (eventBus_) {
            eventBus_->post(EventId::MicronovaValueUpdated, &payload, sizeof(payload), moduleId());
        }
    }

    if (rt.micronova.lastUpdateMs != nowMs) {
        rt.micronova.lastUpdateMs = nowMs;
        dataStore_->notifyChanged(DataKeys::MicronovaLastUpdateMs);
    }
}

bool MicronovaBoilerModule::writeCommand_(const CommandConfig& command, MicronovaWriteTxMode txMode)
{
    if (!bus_) return false;
    const uint8_t repeat = command.repeatCount <= 0 ? 1U : (uint8_t)command.repeatCount;
    const uint16_t repeatDelay = command.repeatDelayMs <= 0 ? MicronovaProtocol::DefaultRepeatDelayMs : (uint16_t)command.repeatDelayMs;
    const bool ok = bus_->queueWrite((uint8_t)command.writeCode,
                                     (uint8_t)command.address,
                                     (uint8_t)command.value,
                                     repeat,
                                     repeatDelay,
                                     txMode);
    LOGI("Micronova cmd queueWrite code=0x%02X addr=0x%02X value=0x%02X repeat=%u delay_ms=%u ok=%d",
         (unsigned)command.writeCode,
         (unsigned)command.address,
         (unsigned)command.value,
         (unsigned)repeat,
         (unsigned)repeatDelay,
         ok ? 1 : 0);
    return ok;
}

bool MicronovaBoilerModule::writeRegister_(MicronovaRegisterId id, uint8_t value)
{
    if (!bus_) return false;
    const RegisterConfig& r = reg_(id);
    if (!r.enabled || !r.writable) return false;
    const bool ok = bus_->queueWrite((uint8_t)r.writeCode, (uint8_t)r.address, value, 1, MicronovaProtocol::DefaultRepeatDelayMs);
    LOGI("Micronova reg queueWrite key=%s write=0x%02X addr=0x%02X value=0x%02X ok=%d",
         kMicronovaDefaultRegisters[(uint8_t)id].key,
         (unsigned)r.writeCode,
         (unsigned)r.address,
         (unsigned)value,
         ok ? 1 : 0);
    return ok;
}

void MicronovaBoilerModule::recordLastCommand_(const char* command)
{
    if (dataStore_) {
        RuntimeData& rt = dataStore_->dataMutable();
        if (strncmp(rt.micronova.lastCommand, command, sizeof(rt.micronova.lastCommand)) != 0) {
            copyText(rt.micronova.lastCommand, sizeof(rt.micronova.lastCommand), command);
            dataStore_->notifyChanged(DataKeys::MicronovaLastCommand);
        }
    }
    fastCyclesRemaining_ = fastCyclesCfg_ > 0 ? (uint16_t)fastCyclesCfg_ : 30U;
    nextPollMs_ = millis();
}

bool MicronovaBoilerModule::setPower(bool on)
{
    int32_t stoveStateCode = -1;
    if (dataStore_) {
        stoveStateCode = dataStore_->data().micronova.stoveStateCode;
    }
    if (!shouldSendIrToggle_(on, stoveStateCode)) {
        LOGI("Micronova IR power toggle skipped request_on=%d stove_state=%ld",
             on ? 1 : 0,
             (long)stoveStateCode);
        return true;
    }

    // Keep IR bursts contiguous: drop pending telemetry reads first.
    bus_->clearReadQueue();

    const bool ok = writeCommand_(on ? powerOn_ : powerOff_);
    if (ok) recordLastCommand_(on ? "power_on" : "power_off");
    return ok;
}

bool MicronovaBoilerModule::setPowerLevel(uint8_t level)
{
    const bool ok = writeRegister_(MicronovaRegisterId::PowerLevel, clampLevel_(level));
    if (ok) recordLastCommand_("power_level");
    return ok;
}

bool MicronovaBoilerModule::setFanSpeed(uint8_t level)
{
    const bool ok = writeRegister_(MicronovaRegisterId::FanSpeed, clampLevel_(level));
    if (ok) recordLastCommand_("fan_speed");
    return ok;
}

bool MicronovaBoilerModule::setTargetTemperature(uint8_t temperature)
{
    const RegisterConfig& r = reg_(MicronovaRegisterId::TargetTemperature);
    uint8_t rawValue = temperature;
    if (r.scale > 0.0f && !floatSame(r.scale, 1.0f)) {
        const float scaled = ((float)temperature - r.offset) / r.scale;
        const int32_t quantized = (int32_t)lroundf(scaled);
        rawValue = (uint8_t)constrain(quantized, 0, 255);
    }
    const bool ok = writeRegister_(MicronovaRegisterId::TargetTemperature, rawValue);
    if (ok) recordLastCommand_("target_temperature");
    return ok;
}

bool MicronovaBoilerModule::sendPowerPlus()
{
    if (!bus_) return false;
    bus_->clearReadQueue();
    const bool ok = writeCommand_(powerPlus_);
    if (ok) recordLastCommand_("power_plus");
    return ok;
}

bool MicronovaBoilerModule::sendPowerMinus()
{
    if (!bus_) return false;
    bus_->clearReadQueue();
    const bool ok = writeCommand_(powerMinus_);
    if (ok) recordLastCommand_("power_minus");
    return ok;
}

bool MicronovaBoilerModule::refreshNow()
{
    fastCyclesRemaining_ = fastCyclesCfg_ > 0 ? (uint16_t)fastCyclesCfg_ : 30U;
    nextPollMs_ = millis();
    recordLastCommand_("refresh");
    return true;
}

void MicronovaBoilerModule::onEventStatic_(const Event& e, void* user)
{
    MicronovaBoilerModule* self = static_cast<MicronovaBoilerModule*>(user);
    if (self) self->handleCommandEvent_(e);
}

void MicronovaBoilerModule::handleCommandEvent_(const Event& e)
{
    if ((readAllSweepActive_ || readAllSweepRequested_) && e.id != EventId::MicronovaCommandSweepReadAll) {
        LOGW("Micronova command ignored: read-all sweep in progress event=%u", (unsigned)e.id);
        return;
    }

    switch (e.id) {
        case EventId::MicronovaCommandPower: {
            if (e.len < sizeof(MicronovaCommandPowerPayload) || !e.payload) return;
            const MicronovaCommandPowerPayload* p = (const MicronovaCommandPowerPayload*)e.payload;
            const bool on = (p->on != 0U);
            const bool ok = setPower(on);
            LOGI("Micronova event power on=%d applied=%d", on ? 1 : 0, ok ? 1 : 0);
            break;
        }
        case EventId::MicronovaCommandPowerLevel: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            const bool ok = setPowerLevel(p->value);
            LOGI("Micronova event power_level value=%u applied=%d", (unsigned)p->value, ok ? 1 : 0);
            break;
        }
        case EventId::MicronovaCommandFanSpeed: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            (void)p;
            LOGW("Micronova event fan_speed ignored: unsupported on this stove");
            break;
        }
        case EventId::MicronovaCommandTargetTemperature: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            const bool ok = setTargetTemperature(p->value);
            LOGI("Micronova event target_temperature value=%u applied=%d", (unsigned)p->value, ok ? 1 : 0);
            break;
        }
        case EventId::MicronovaCommandRefresh:
            (void)refreshNow();
            LOGI("Micronova event refresh");
            break;
        case EventId::MicronovaCommandRawWrite: {
            if (e.len < sizeof(MicronovaCommandRawWritePayload) || !e.payload || !bus_) return;
            const MicronovaCommandRawWritePayload* p = (const MicronovaCommandRawWritePayload*)e.payload;
            const bool ok = bus_->queueWrite(p->writeCode, p->address, p->value, p->repeatCount, p->repeatDelayMs);
            LOGI("Micronova event raw_write code=0x%02X addr=0x%02X value=0x%02X repeat=%u delay_ms=%u applied=%d",
                 (unsigned)p->writeCode,
                 (unsigned)p->address,
                 (unsigned)p->value,
                 (unsigned)p->repeatCount,
                 (unsigned)p->repeatDelayMs,
                 ok ? 1 : 0);
            recordLastCommand_("raw_write");
            break;
        }
        case EventId::MicronovaCommandSweepReadAll:
            requestReadAllSweep_();
            recordLastCommand_("read_all_sweep");
            break;
        case EventId::MicronovaCommandPowerPlus: {
            const bool ok = sendPowerPlus();
            LOGI("Micronova event power_plus applied=%d", ok ? 1 : 0);
            break;
        }
        case EventId::MicronovaCommandPowerMinus: {
            const bool ok = sendPowerMinus();
            LOGI("Micronova event power_minus applied=%d", ok ? 1 : 0);
            break;
        }
        default:
            break;
    }
}
