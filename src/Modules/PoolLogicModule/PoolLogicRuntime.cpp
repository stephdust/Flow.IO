/**
 * @file PoolLogicRuntime.cpp
 * @brief Runtime and config publication helpers for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"

#include <Arduino.h>
#include <cstring>
#include <stdio.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
// The aggregated cfg payload republishes the same branch split used by the
// config routes so MQTT consumers can fetch one coherent snapshot.
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
static constexpr const char* kCfgModuleMode = "poollogic/mode";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModulePid = "poollogic/pid";
static constexpr const char* kCfgModuleDelay = "poollogic/delay";
static constexpr const char* kCfgModuleDevice = "poollogic/device";
}

MqttBuildResult PoolLogicModule::buildCfgBaseStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->buildCfgBase_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolLogicModule::buildCfgBase_(MqttBuildContext& buildCtx)
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
                                                     kPoolLogicCfgTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    struct Entry {
        const char* key;
        const char* moduleName;
    };
    static constexpr Entry kEntries[] = {
        {"mode", kCfgModuleMode},
        {"filtration", kCfgModuleFiltration},
        {"sensors", kCfgModuleSensors},
        {"pid", kCfgModulePid},
        {"delay", kCfgModuleDelay},
        {"device", kCfgModuleDevice},
    };

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    // Rebuild the aggregate JSON from per-branch module snapshots so the
    // payload stays a pure projection of the config store.
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(kEntries[i].moduleName,
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
                               kEntries[i].key,
                               moduleJson);
        if (!(w > 0 && (size_t)w < (buildCtx.payloadCapacity - pos))) {
            truncatedPayload = true;
            break;
        }
        pos += (size_t)w;
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/poollogic")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolLogicCfgTopicBase);
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

uint8_t PoolLogicModule::runtimeSnapshotCount() const
{
    return 2;
}

const char* PoolLogicModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    if (idx == 0) return "rt/poollogic/ph";
    if (idx == 1) return "rt/poollogic/orp";
    return nullptr;
}

RuntimeRouteClass PoolLogicModule::runtimeSnapshotClass(uint8_t idx) const
{
    (void)idx;
    return RuntimeRouteClass::NumericThrottled;
}

bool PoolLogicModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (idx > 1) return false;
    if (key >= DATAKEY_IO_BASE && key < (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return true;
    if (key >= DATAKEY_POOL_DEVICE_STATE_BASE &&
        key < (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + POOL_DEVICE_MAX)) return true;
    return false;
}

bool PoolLogicModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    const uint32_t nowMs = millis();
    const bool isPh = (idx == 0);
    const bool isOrp = (idx == 1);
    if (!isPh && !isOrp) return false;

    const TemporalPidState& st = isPh ? phPidState_ : orpPidState_;
    const DeviceFsm& pumpFsm = isPh ? phPumpFsm_ : orpPumpFsm_;
    const float kp = isPh ? phKp_ : orpKp_;
    const float ki = isPh ? phKi_ : orpKi_;
    const float kd = isPh ? phKd_ : orpKd_;
    const int32_t windowMsCfg = isPh ? phWindowMs_ : orpWindowMs_;
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;

    uint32_t elapsedMs = 0;
    if (st.initialized) {
        const uint32_t rawElapsed = nowMs - st.windowStartMs;
        elapsedMs = (rawElapsed < windowMs) ? rawElapsed : windowMs;
    }

    // Runtime snapshots expose the last PID sample plus the derived on-window
    // so MQTT/UI consumers can inspect why dosing is currently active or idle.
    const bool regulationEnabled = isPh ? phPidEnabled_ : orpPidEnabled_;
    int wrote = 0;
    if (st.sampleValid) {
        wrote = snprintf(
            out, len,
            "{\"i\":\"%s\",\"in\":%.3f,\"sp\":%.3f,\"er\":%.3f,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":%lu,"
            "\"em\":%s,\"t\":%lu}",
            isPh ? "ph" : "orp",
            (double)st.sampleInput,
            (double)st.sampleSetpoint,
            (double)st.sampleError,
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned long)st.sampleTsMs,
            electrolyseMode_ ? "true" : "false",
            (unsigned long)nowMs
        );
    } else {
        wrote = snprintf(
            out, len,
            "{\"i\":\"%s\",\"in\":null,\"sp\":%.3f,\"er\":null,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":0,"
            "\"em\":%s,\"t\":%lu}",
            isPh ? "ph" : "orp",
            (double)(isPh ? phSetpoint_ : orpSetpoint_),
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            electrolyseMode_ ? "true" : "false",
            (unsigned long)nowMs
        );
    }
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = st.runtimeTsMs ? st.runtimeTsMs : 1U;
    return true;
}
