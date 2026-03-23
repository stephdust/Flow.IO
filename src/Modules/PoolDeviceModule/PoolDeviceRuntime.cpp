/**
 * @file PoolDeviceRuntime.cpp
 * @brief Runtime snapshots, config publications and runtime bootstrap helpers.
 */

#include "PoolDeviceModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace {
static constexpr const char* kPoolDeviceCfgTopicBase = "cfg/pdm";
static constexpr const char* kPoolDeviceCfgRuntimeTopicBase = "cfg/pdmrt";

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

bool isFiniteNonNegative_(float value)
{
    return isfinite(value) && value >= 0.0f;
}
} // namespace

bool PoolDeviceModule::defineDevice(const PoolDeviceDefinition& def)
{
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) continue;

        PoolDeviceSlot& s = slots_[i];
        s.used = true;
        s.def = def;
        snprintf(s.id, sizeof(s.id), "pd%u", (unsigned)i);

        if (s.def.label[0] == '\0') {
            strncpy(s.def.label, s.id, sizeof(s.def.label) - 1);
            s.def.label[sizeof(s.def.label) - 1] = '\0';
        }
        if (s.def.ioId == IO_ID_INVALID) {
            LOGW("Pool device %s missing ioId", s.id);
            s.used = false;
            return false;
        }
        if (s.def.maxUptimeDaySec < 0) {
            s.def.maxUptimeDaySec = 0;
        }
        s.ioId = s.def.ioId;
        s.desiredOn = false;
        s.actualOn = false;
        s.blockReason = POOL_DEVICE_BLOCK_NONE;

        if (s.def.tankCapacityMl > 0.0f) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            s.tankRemainingMl = 0.0f;
        }
        s.dayKey = 0;
        s.weekKey = 0;
        s.monthKey = 0;
        s.hasPersistedMetrics = false;
        s.persistDirty = false;
        s.persistImmediate = false;

        return true;
    }
    return false;
}

const char* PoolDeviceModule::deviceLabel(uint8_t idx) const
{
    if (idx >= POOL_DEVICE_MAX) return nullptr;
    const PoolDeviceSlot& s = slots_[idx];
    if (!s.used) return nullptr;
    return (s.def.label[0] != '\0') ? s.def.label : s.id;
}

const char* PoolDeviceModule::typeStr_(uint8_t type)
{
    if (type == POOL_DEVICE_RT_FILTRATION) return "filtration";
    if (type == POOL_DEVICE_RT_PERISTALTIC) return "peristaltic";
    return "relay";
}

const char* PoolDeviceModule::blockReasonStr_(uint8_t reason)
{
    if (reason == POOL_DEVICE_BLOCK_DISABLED) return "disabled";
    if (reason == POOL_DEVICE_BLOCK_INTERLOCK) return "interlock";
    if (reason == POOL_DEVICE_BLOCK_IO_ERROR) return "io_error";
    if (reason == POOL_DEVICE_BLOCK_MAX_UPTIME) return "max_uptime";
    return "none";
}

uint8_t PoolDeviceModule::runtimeSnapshotCount() const
{
    return (uint8_t)(activeCount_() * 2U);
}

bool PoolDeviceModule::snapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut, bool& metricsOut) const
{
    uint8_t seen = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = false;
            return true;
        }
        ++seen;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = true;
            return true;
        }
        ++seen;
    }
    return false;
}

bool PoolDeviceModule::buildStateSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slots_[slotIdx].used) return false;

    PoolDeviceRuntimeStateEntry entry{};
    if (!poolDeviceRuntimeState(*dataStore_, slotIdx, entry)) return false;

    const char* label = deviceLabel(slotIdx);
    const char* typeStr = typeStr_(entry.type);
    const char* blockReason = blockReasonStr_(entry.blockReason);
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\",\"enabled\":%s,\"desired\":%s,\"on\":%s,"
        "\"type\":\"%s\",\"block\":\"%s\",\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        entry.enabled ? "true" : "false",
        entry.desiredOn ? "true" : "false",
        entry.actualOn ? "true" : "false",
        typeStr,
        blockReason,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    return true;
}

bool PoolDeviceModule::buildMetricsSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slots_[slotIdx].used) return false;

    PoolDeviceRuntimeMetricsEntry entry{};
    if (!poolDeviceRuntimeMetrics(*dataStore_, slotIdx, entry)) return false;

    const char* label = deviceLabel(slotIdx);
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\","
        "\"running\":{\"day_s\":%lu,\"week_s\":%lu,\"month_s\":%lu,\"total_s\":%lu},"
        "\"injected\":{\"day_ml\":%.3f,\"week_ml\":%.3f,\"month_ml\":%.3f,\"total_ml\":%.3f},"
        "\"tank\":{\"remaining_ml\":%.3f},\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        (unsigned long)entry.runningSecDay,
        (unsigned long)entry.runningSecWeek,
        (unsigned long)entry.runningSecMonth,
        (unsigned long)entry.runningSecTotal,
        (double)entry.injectedMlDay,
        (double)entry.injectedMlWeek,
        (double)entry.injectedMlMonth,
        (double)entry.injectedMlTotal,
        (double)entry.tankRemainingMl,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    return true;
}

const char* PoolDeviceModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return nullptr;

    static char suffix[32];
    if (metrics) {
        snprintf(suffix, sizeof(suffix), "rt/pdm/metrics/pd%u", (unsigned)slotIdx);
    } else {
        snprintf(suffix, sizeof(suffix), "rt/pdm/state/pd%u", (unsigned)slotIdx);
    }
    return suffix;
}

RuntimeRouteClass PoolDeviceModule::runtimeSnapshotClass(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    (void)slotIdx;
    return metrics ? RuntimeRouteClass::NumericThrottled : RuntimeRouteClass::ActuatorImmediate;
}

bool PoolDeviceModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;

    const DataKey expected = metrics
        ? (DataKey)(DATAKEY_POOL_DEVICE_METRICS_BASE + slotIdx)
        : (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + slotIdx);
    return key == expected;
}

bool PoolDeviceModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;
    return metrics
        ? buildMetricsSnapshot_(slotIdx, out, len, maxTsOut)
        : buildStateSnapshot_(slotIdx, out, len, maxTsOut);
}

bool PoolDeviceModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!ioSvc_) return false;

    const uint32_t now = millis();
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        IoEndpointMeta meta{};
        if (ioSvc_->meta(ioSvc_->ctx, s.ioId, &meta) != IO_OK) {
            LOGW("Pool device %s invalid ioId=%u", s.id, (unsigned)s.ioId);
            return false;
        }
        if (meta.kind != IO_KIND_DIGITAL_OUT || (meta.capabilities & IO_CAP_W) == 0) {
            LOGW("Pool device %s ioId=%u is not writable digital output", s.id, (unsigned)s.ioId);
            return false;
        }

        if (s.def.tankCapacityMl <= 0.0f) {
            s.tankRemainingMl = 0.0f;
        } else if (!s.hasPersistedMetrics) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            if (!isFiniteNonNegative_(s.tankRemainingMl)) s.tankRemainingMl = 0.0f;
            if (s.tankRemainingMl > s.def.tankCapacityMl) s.tankRemainingMl = s.def.tankCapacityMl;
        }

        s.lastTickMs = now;
        s.stateTsMs = now;
        s.metricsTsMs = now;
        s.lastRuntimeCommitMs = now;
        s.lastPersistMs = now;

        PoolDeviceRuntimeStateEntry rtState{};
        rtState.valid = true;
        rtState.enabled = s.def.enabled;
        rtState.desiredOn = s.desiredOn;
        rtState.actualOn = s.actualOn;
        rtState.type = s.def.type;
        rtState.blockReason = s.blockReason;
        rtState.tsMs = s.stateTsMs;

        PoolDeviceRuntimeMetricsEntry rtMetrics{};
        rtMetrics.valid = true;
        rtMetrics.runningSecDay = toSeconds_(s.runningMsDay);
        rtMetrics.runningSecWeek = toSeconds_(s.runningMsWeek);
        rtMetrics.runningSecMonth = toSeconds_(s.runningMsMonth);
        rtMetrics.runningSecTotal = toSeconds_(s.runningMsTotal);
        rtMetrics.injectedMlDay = s.injectedMlDay;
        rtMetrics.injectedMlWeek = s.injectedMlWeek;
        rtMetrics.injectedMlMonth = s.injectedMlMonth;
        rtMetrics.injectedMlTotal = s.injectedMlTotal;
        rtMetrics.tankRemainingMl = s.tankRemainingMl;
        rtMetrics.tsMs = s.metricsTsMs;
        if (dataStore_) {
            (void)setPoolDeviceRuntimeState(*dataStore_, i, rtState);
            (void)setPoolDeviceRuntimeMetrics(*dataStore_, i, rtMetrics);
        }
    }

    runtimeReady_ = true;
    return true;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdmStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    return self ? self->buildCfgBasePdm_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdmrtStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    return self ? self->buildCfgBasePdmrt_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdm_(MqttBuildContext& buildCtx)
{
    if (!cfgStore_) return MqttBuildResult::RetryLater;
    if (!buildCtx.topic || buildCtx.topicCapacity == 0U || !buildCtx.payload || buildCtx.payloadCapacity == 0U) {
        return MqttBuildResult::PermanentError;
    }
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::RetryLater;

    char relativeTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    size_t topicLen = 0U;
    if (!MqttConfigRouteProducer::buildRelativeTopic(relativeTopic,
                                                     sizeof(relativeTopic),
                                                     kPoolDeviceCfgTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;

        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(cfgModuleName_[i],
                                                    moduleJson,
                                                    sizeof(moduleJson),
                                                    &truncatedModule);
        if (truncatedModule) {
            truncatedPayload = true;
            break;
        }
        if (!hasAny) continue;

        const int w = snprintf(buildCtx.payload + pos,
                               buildCtx.payloadCapacity - pos,
                               "%s\"%s\":%s",
                               any ? "," : "",
                               slots_[i].id,
                               moduleJson);
        if (!(w > 0 && (size_t)w < (buildCtx.payloadCapacity - pos))) {
            truncatedPayload = true;
            break;
        }
        pos += (size_t)w;
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/pdm")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolDeviceCfgTopicBase);
        return MqttBuildResult::NoLongerNeeded;
    }

    buildCtx.payload[pos++] = '}';
    buildCtx.payload[pos] = '\0';
    buildCtx.topicLen = (uint16_t)topicLen;
    buildCtx.payloadLen = (uint16_t)pos;
    buildCtx.qos = 1;
    buildCtx.retain = true;
    return MqttBuildResult::Ready;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdmrt_(MqttBuildContext& buildCtx)
{
    if (!cfgStore_) return MqttBuildResult::RetryLater;
    if (!buildCtx.topic || buildCtx.topicCapacity == 0U || !buildCtx.payload || buildCtx.payloadCapacity == 0U) {
        return MqttBuildResult::PermanentError;
    }
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::RetryLater;

    char relativeTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    size_t topicLen = 0U;
    if (!MqttConfigRouteProducer::buildRelativeTopic(relativeTopic,
                                                     sizeof(relativeTopic),
                                                     kPoolDeviceCfgRuntimeTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;

        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(cfgRuntimeModuleName_[i],
                                                    moduleJson,
                                                    sizeof(moduleJson),
                                                    &truncatedModule);
        if (truncatedModule) {
            truncatedPayload = true;
            break;
        }
        if (!hasAny) continue;

        const int w = snprintf(buildCtx.payload + pos,
                               buildCtx.payloadCapacity - pos,
                               "%s\"%s\":%s",
                               any ? "," : "",
                               slots_[i].id,
                               moduleJson);
        if (!(w > 0 && (size_t)w < (buildCtx.payloadCapacity - pos))) {
            truncatedPayload = true;
            break;
        }
        pos += (size_t)w;
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/pdmrt")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolDeviceCfgRuntimeTopicBase);
        return MqttBuildResult::NoLongerNeeded;
    }

    buildCtx.payload[pos++] = '}';
    buildCtx.payload[pos] = '\0';
    buildCtx.topicLen = (uint16_t)topicLen;
    buildCtx.payloadLen = (uint16_t)pos;
    buildCtx.qos = 1;
    buildCtx.retain = true;
    return MqttBuildResult::Ready;
}
