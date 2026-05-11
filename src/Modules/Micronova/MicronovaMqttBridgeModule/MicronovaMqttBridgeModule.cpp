#include "Modules/Micronova/MicronovaMqttBridgeModule/MicronovaMqttBridgeModule.h"

#include "Core/DataKeys.h"
#include "Core/Services/Services.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaMqttBridgeModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr const char* kSuffixConnection = "micronova/status/connection";
static constexpr const char* kSuffixState = "micronova/status/state";
static constexpr const char* kSuffixOnoff = "micronova/status/onoff";
static constexpr const char* kSuffixPowerState = "micronova/status/power_state";
static constexpr const char* kSuffixPowerLevel = "micronova/status/power_level";
static constexpr const char* kSuffixStoveState = "micronova/status/stove_state";
static constexpr const char* kSuffixStoveStateCode = "micronova/status/stove_state_code";
static constexpr const char* kSuffixAlarmCode = "micronova/status/alarm_code";
static constexpr const char* kSuffixLastCommand = "micronova/status/last_command";
static constexpr const char* kSuffixRoomTemperature = "micronova/sensor/ambtemp";
static constexpr const char* kSuffixFumesTemperature = "micronova/sensor/fumetemp";
static constexpr const char* kSuffixWaterTemperature = "micronova/sensor/water_temperature";
static constexpr const char* kSuffixWaterPressure = "micronova/sensor/water_pressure";
static constexpr const char* kSuffixPowerSensor = "micronova/sensor/power";
static constexpr const char* kSuffixFanSensor = "micronova/sensor/fan";
static constexpr const char* kSuffixTargetTemperature = "micronova/sensor/tempset";
static constexpr const char* kSuffixDisplayLine1 = "micronova/status/display_line_1";
static constexpr const char* kSuffixDisplayLine2 = "micronova/status/display_line_2";
static constexpr const char* kSuffixDisplayLine3 = "micronova/status/display_line_3";

static constexpr const char* kCommandPower = "micronova/command/power/set";
static constexpr const char* kCommandPowerLevel = "micronova/command/power_level/set";
static constexpr const char* kCommandFan = "micronova/command/fan/set";
static constexpr const char* kCommandTemperature = "micronova/command/temperature/set";
static constexpr const char* kCommandRefresh = "micronova/command/refresh";
static constexpr const char* kCommandSweepReadAll = "micronova/command/read_all_sweep";
static constexpr const char* kCommandPowerPlus = "micronova/command/p_plus";
static constexpr const char* kCommandPowerMinus = "micronova/command/p_minus";
static constexpr const char* kCommandCompact = "micronova/command/cmd";
static constexpr const char* kCommandAuxOutput = "micronova/io/output/d00/set";
static constexpr const char* kCommandAuxOutputAlias = "micronova/command/aux_output/set";
static constexpr const char* kCommandAuxOutputRuntime = "rt/io/output/d00/set";

static const char* trim(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return "";
    out[0] = '\0';
    if (!in) return out;
    while (*in && isspace((unsigned char)*in)) ++in;
    size_t n = strnlen(in, outLen - 1U);
    while (n > 0U && isspace((unsigned char)in[n - 1U])) --n;
    memcpy(out, in, n);
    out[n] = '\0';
    return out;
}

static bool equalsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}
}

void MicronovaMqttBridgeModule::init(ConfigStore&, ServiceRegistry& services)
{
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);

    producer_ = MqttPublishProducer{};
    producer_.producerId = ProducerId;
    producer_.ctx = this;
    producer_.buildMessage = &MicronovaMqttBridgeModule::buildMessageStatic_;

    if (mqttSvc_ && mqttSvc_->registerProducer) {
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }
    registerInbound_();

    if (!producerRegistered_) {
        LOGW("Micronova MQTT producer registration failed (producer_id=%u)", (unsigned)ProducerId);
    }
    if (!inboundRegistered_) {
        LOGW("Micronova MQTT inbound registration incomplete");
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) {
        char publishTopic[Limits::Mqtt::Buffers::Topic] = {0};
        char commandTopic[Limits::Mqtt::Buffers::Topic] = {0};
        mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixConnection, publishTopic, sizeof(publishTopic));
        mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPower, commandTopic, sizeof(commandTopic));
        LOGI("Micronova MQTT bridge ready producer_id=%u producer_ok=%d inbound_ok=%d pub='%s' cmd='%s'",
             (unsigned)ProducerId,
             producerRegistered_ ? 1 : 0,
             inboundRegistered_ ? 1 : 0,
             publishTopic,
             commandTopic);
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &MicronovaMqttBridgeModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaValueUpdated, &MicronovaMqttBridgeModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaOnlineChanged, &MicronovaMqttBridgeModule::onEventStatic_, this);
    }

    // If MQTT is already connected before this module subscribes to DataChanged,
    // force one full retained publication pass so all Micronova topics exist.
    if (dataStore_ && mqttReady(*dataStore_)) {
        enqueueAll_(MqttPublishPriority::High);
    }
}

void MicronovaMqttBridgeModule::registerInbound_()
{
    if (inboundRegistered_ || !mqttSvc_ || !mqttSvc_->registerInboundHandler) return;
    static constexpr const char* kTopics[InboundCount] = {
        kCommandPower,
        kCommandPowerLevel,
        kCommandFan,
        kCommandTemperature,
        kCommandRefresh,
        kCommandSweepReadAll,
        kCommandPowerPlus,
        kCommandPowerMinus,
        kCommandCompact,
        kCommandAuxOutput,
        kCommandAuxOutputAlias,
        kCommandAuxOutputRuntime
    };
    bool ok = true;
    for (uint8_t i = 0; i < InboundCount; ++i) {
        inbound_[i] = MqttInboundHandler{kTopics[i], this, &MicronovaMqttBridgeModule::onInboundStatic_};
        ok = mqttSvc_->registerInboundHandler(mqttSvc_->ctx, &inbound_[i]) && ok;
    }
    inboundRegistered_ = ok;
}

void MicronovaMqttBridgeModule::onEventStatic_(const Event& e, void* user)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(user);
    if (self) self->onEvent_(e);
}

void MicronovaMqttBridgeModule::onEvent_(const Event& e)
{
    if (e.id == EventId::MicronovaOnlineChanged) {
        (void)enqueueMsg_(MsgConnection, MqttPublishPriority::High);
        return;
    }
    if (e.id == EventId::MicronovaValueUpdated) {
        if (e.payload && e.len >= sizeof(MicronovaValueUpdatedPayload)) {
            const MicronovaValueUpdatedPayload* p = (const MicronovaValueUpdatedPayload*)e.payload;
            enqueueForMicronovaKey_(p->key);
        }
        return;
    }
    if (e.id != EventId::DataChanged || e.len < sizeof(DataChangedPayload) || !e.payload) return;
    const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
    if (p->id == DATAKEY_MQTT_READY) {
        if (dataStore_ && mqttReady(*dataStore_)) {
            enqueueAll_(MqttPublishPriority::High);
            if (!topicsLogged_ && mqttSvc_ && mqttSvc_->formatTopic) {
                topicsLogged_ = true;
                char topic[Limits::Mqtt::Buffers::Topic] = {0};
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixConnection, topic, sizeof(topic));
                LOGI("Micronova MQTT publish base topic example: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixState, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixOnoff, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixRoomTemperature, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixFumesTemperature, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixTargetTemperature, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixPowerSensor, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixFanSensor, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixAlarmCode, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixDisplayLine1, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixDisplayLine2, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
                mqttSvc_->formatTopic(mqttSvc_->ctx, kSuffixDisplayLine3, topic, sizeof(topic));
                LOGI("Micronova MQTT topic: %s", topic);
            }
        }
        return;
    }
    enqueueForKey_(p->id);
}

void MicronovaMqttBridgeModule::enqueueAll_(MqttPublishPriority priority)
{
    if (!producerRegistered_ || !mqttSvc_ || !mqttSvc_->enqueue) return;
    for (uint16_t msg = MsgConnection; msg < MsgCount; ++msg) {
        (void)enqueueMsg_(msg, priority);
    }
}

void MicronovaMqttBridgeModule::enqueueForKey_(DataKey key)
{
    if (!producerRegistered_ || !mqttSvc_ || !mqttSvc_->enqueue) return;
    uint16_t msgs[4] = {0};
    uint8_t n = 0;
    switch (key) {
        case DataKeys::MicronovaOnline: msgs[n++] = MsgConnection; break;
        case DataKeys::MicronovaStoveStateText: msgs[n++] = MsgState; msgs[n++] = MsgStoveState; break;
        case DataKeys::MicronovaStoveStateCode: msgs[n++] = MsgStoveStateCode; break;
        case DataKeys::MicronovaPowerState: msgs[n++] = MsgOnoff; msgs[n++] = MsgPowerState; break;
        case DataKeys::MicronovaPowerLevel: msgs[n++] = MsgPowerLevel; msgs[n++] = MsgPowerSensor; break;
        case DataKeys::MicronovaFanSpeed: msgs[n++] = MsgFanSensor; break;
        case DataKeys::MicronovaTargetTemperature: msgs[n++] = MsgTargetTemperature; break;
        case DataKeys::MicronovaRoomTemperature: msgs[n++] = MsgRoomTemperature; break;
        case DataKeys::MicronovaFumesTemperature: msgs[n++] = MsgFumesTemperature; break;
        case DataKeys::MicronovaWaterTemperature: msgs[n++] = MsgWaterTemperature; break;
        case DataKeys::MicronovaWaterPressure: msgs[n++] = MsgWaterPressure; break;
        case DataKeys::MicronovaAlarmCode: msgs[n++] = MsgAlarmCode; break;
        case DataKeys::MicronovaLastCommand: msgs[n++] = MsgLastCommand; break;
        case DataKeys::MicronovaDisplayLine1: msgs[n++] = MsgDisplayLine1; break;
        case DataKeys::MicronovaDisplayLine2: msgs[n++] = MsgDisplayLine2; break;
        case DataKeys::MicronovaDisplayLine3: msgs[n++] = MsgDisplayLine3; break;
        default: return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        (void)enqueueMsg_(msgs[i], MqttPublishPriority::High);
    }
}

void MicronovaMqttBridgeModule::enqueueForMicronovaKey_(const char* key)
{
    if (!key || key[0] == '\0') return;
    LOGI("Micronova MQTT enqueue from key='%s'", key);
    if (strcmp(key, "stove_state") == 0) {
        (void)enqueueMsg_(MsgState, MqttPublishPriority::High);
        (void)enqueueMsg_(MsgStoveState, MqttPublishPriority::High);
        (void)enqueueMsg_(MsgStoveStateCode, MqttPublishPriority::High);
        (void)enqueueMsg_(MsgOnoff, MqttPublishPriority::High);
        (void)enqueueMsg_(MsgPowerState, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "room_temperature") == 0) {
        (void)enqueueMsg_(MsgRoomTemperature, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "fumes_temperature") == 0) {
        (void)enqueueMsg_(MsgFumesTemperature, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "power_level") == 0) {
        (void)enqueueMsg_(MsgPowerLevel, MqttPublishPriority::High);
        (void)enqueueMsg_(MsgPowerSensor, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "fan_speed") == 0) {
        (void)enqueueMsg_(MsgFanSensor, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "target_temperature") == 0) {
        (void)enqueueMsg_(MsgTargetTemperature, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "water_temperature") == 0) {
        (void)enqueueMsg_(MsgWaterTemperature, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "water_pressure") == 0) {
        (void)enqueueMsg_(MsgWaterPressure, MqttPublishPriority::High);
        return;
    }
    if (strcmp(key, "alarm_code") == 0) {
        (void)enqueueMsg_(MsgAlarmCode, MqttPublishPriority::High);
        return;
    }
    LOGW("Micronova MQTT key not mapped key='%s'", key);
}

bool MicronovaMqttBridgeModule::enqueueMsg_(uint16_t msgId, MqttPublishPriority priority)
{
    if (!producerRegistered_ || !mqttSvc_ || !mqttSvc_->enqueue) return false;
    const bool accepted = mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, msgId, (uint8_t)priority, 0);
    if (!accepted) {
        const bool connected = (mqttSvc_->isConnected && mqttSvc_->isConnected(mqttSvc_->ctx));
        LOGW("MQTT enqueue failed producer=%u msg=%u prio=%u connected=%d",
             (unsigned)ProducerId,
             (unsigned)msgId,
             (unsigned)priority,
             connected ? 1 : 0);
    }
    return accepted;
}

MqttBuildResult MicronovaMqttBridgeModule::buildMessageStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(ctx);
    return self ? self->buildMessage_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

bool MicronovaMqttBridgeModule::publishText_(MqttBuildContext& ctx, const char* suffix, const char* text, bool retain)
{
    if (!mqttSvc_ || !mqttSvc_->formatTopic || !suffix || !text) return false;
    mqttSvc_->formatTopic(mqttSvc_->ctx, suffix, ctx.topic, ctx.topicCapacity);
    const int pw = snprintf(ctx.payload, ctx.payloadCapacity, "%s", text);
    if (!(pw >= 0 && (uint16_t)pw < ctx.payloadCapacity)) return false;
    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
    ctx.payloadLen = (uint16_t)pw;
    ctx.qos = 0;
    ctx.retain = retain;
    return ctx.topicLen > 0U;
}

bool MicronovaMqttBridgeModule::publishInt_(MqttBuildContext& ctx, const char* suffix, int32_t value, bool retain)
{
    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%ld", (long)value);
    return publishText_(ctx, suffix, buf, retain);
}

bool MicronovaMqttBridgeModule::publishFloat_(MqttBuildContext& ctx, const char* suffix, float value, bool retain)
{
    char buf[24] = {0};
    snprintf(buf, sizeof(buf), "%.2f", (double)value);
    return publishText_(ctx, suffix, buf, retain);
}

MqttBuildResult MicronovaMqttBridgeModule::buildMessage_(uint16_t messageId, MqttBuildContext& ctx)
{
    if (!dataStore_) return MqttBuildResult::RetryLater;
    const MicronovaRuntimeData& rt = dataStore_->data().micronova;
    bool ok = false;
    switch (messageId) {
        case MsgConnection: ok = publishText_(ctx, kSuffixConnection, rt.online ? "online" : "offline", true); break;
        case MsgState: ok = publishText_(ctx, kSuffixState, rt.stoveStateText, true); break;
        case MsgOnoff: ok = publishText_(ctx, kSuffixOnoff, rt.powerState, true); break;
        case MsgPowerState: ok = publishText_(ctx, kSuffixPowerState, rt.powerState, true); break;
        case MsgPowerLevel: ok = publishInt_(ctx, kSuffixPowerLevel, rt.powerLevel, true); break;
        case MsgStoveState: ok = publishText_(ctx, kSuffixStoveState, rt.stoveStateText, true); break;
        case MsgStoveStateCode: ok = publishInt_(ctx, kSuffixStoveStateCode, rt.stoveStateCode, true); break;
        case MsgAlarmCode: ok = publishInt_(ctx, kSuffixAlarmCode, rt.alarmCode, true); break;
        case MsgLastCommand: ok = publishText_(ctx, kSuffixLastCommand, rt.lastCommand, true); break;
        case MsgRoomTemperature: ok = publishFloat_(ctx, kSuffixRoomTemperature, rt.roomTemperature, true); break;
        case MsgFumesTemperature: ok = publishFloat_(ctx, kSuffixFumesTemperature, rt.fumesTemperature, true); break;
        case MsgWaterTemperature: ok = publishFloat_(ctx, kSuffixWaterTemperature, rt.waterTemperature, true); break;
        case MsgWaterPressure: ok = publishFloat_(ctx, kSuffixWaterPressure, rt.waterPressure, true); break;
        case MsgPowerSensor: ok = publishInt_(ctx, kSuffixPowerSensor, rt.powerLevel, true); break;
        case MsgFanSensor: ok = publishInt_(ctx, kSuffixFanSensor, rt.fanSpeed, true); break;
        case MsgTargetTemperature: ok = publishInt_(ctx, kSuffixTargetTemperature, rt.targetTemperature, true); break;
        case MsgDisplayLine1: ok = publishText_(ctx, kSuffixDisplayLine1, rt.displayLine1, true); break;
        case MsgDisplayLine2: ok = publishText_(ctx, kSuffixDisplayLine2, rt.displayLine2, true); break;
        case MsgDisplayLine3: ok = publishText_(ctx, kSuffixDisplayLine3, rt.displayLine3, true); break;
        default: return MqttBuildResult::NoLongerNeeded;
    }
    return ok ? MqttBuildResult::Ready : MqttBuildResult::PermanentError;
}

void MicronovaMqttBridgeModule::onInboundStatic_(void* ctx, const MqttInboundMessage& message)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(ctx);
    if (self) self->onInbound_(message);
}

bool MicronovaMqttBridgeModule::postValueCommand_(EventId eventId, uint8_t value)
{
    if (!eventBus_) return false;
    MicronovaCommandValuePayload payload{value};
    return eventBus_->post(eventId, &payload, sizeof(payload), moduleId());
}

bool MicronovaMqttBridgeModule::setAuxOutput_(bool on)
{
    if (!ioSvc_) {
        LOGW("MQTT aux_output failed: IO service unavailable");
        return false;
    }
    if (!ioSvc_->writeDigital) {
        LOGW("MQTT aux_output failed: IO writeDigital unavailable");
        return false;
    }
    const IoStatus status = ioSvc_->writeDigital(ioSvc_->ctx, (IoId)(IO_ID_DO_BASE + 0), on ? 1U : 0U, millis());
    if (status != IO_OK) {
        LOGW("MQTT aux_output failed: io_status=%d", (int)status);
        return false;
    }
    return true;
}

int MicronovaMqttBridgeModule::parseInt_(const char* payload, bool& ok)
{
    ok = false;
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (buf[0] == '\0') return 0;
    char* end = nullptr;
    const long v = strtol(buf, &end, 10);
    if (end == buf || (end && *end != '\0')) return 0;
    ok = true;
    return (int)v;
}

bool MicronovaMqttBridgeModule::parsePower_(const char* payload, bool& out)
{
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (equalsIgnoreCase(buf, "ON") || strcmp(buf, "1") == 0 || equalsIgnoreCase(buf, "true")) {
        out = true;
        return true;
    }
    if (equalsIgnoreCase(buf, "OFF") || strcmp(buf, "0") == 0 || equalsIgnoreCase(buf, "false")) {
        out = false;
        return true;
    }
    return false;
}

bool MicronovaMqttBridgeModule::parseCompact_(const char* payload)
{
    if (!eventBus_) return false;
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (buf[0] == '\0') return false;
    for (char* p = buf; *p; ++p) *p = (char)toupper((unsigned char)*p);

    if (strcmp(buf, "ON") == 0) {
        MicronovaCommandPowerPayload p{1};
        const bool ok = eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
        LOGI("MQTT compact cmd '%s' -> power on post=%d", buf, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(buf, "OFF") == 0 || strcmp(buf, "E") == 0) {
        MicronovaCommandPowerPayload p{0};
        const bool ok = eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
        LOGI("MQTT compact cmd '%s' -> power off post=%d", buf, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(buf, "P+") == 0) {
        const bool ok = eventBus_->post(EventId::MicronovaCommandPowerPlus, nullptr, 0, moduleId());
        LOGI("MQTT compact cmd '%s' -> power_plus post=%d", buf, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(buf, "P-") == 0) {
        const bool ok = eventBus_->post(EventId::MicronovaCommandPowerMinus, nullptr, 0, moduleId());
        LOGI("MQTT compact cmd '%s' -> power_minus post=%d", buf, ok ? 1 : 0);
        return ok;
    }
    if ((buf[0] == 'P' || buf[0] == 'F' || buf[0] == 'T') && buf[1] != '\0') {
        bool ok = false;
        const int value = parseInt_(buf + 1, ok);
        if (!ok) return false;
        bool posted = false;
        if (buf[0] == 'P') posted = postValueCommand_(EventId::MicronovaCommandPowerLevel, (uint8_t)value);
        else if (buf[0] == 'F') posted = postValueCommand_(EventId::MicronovaCommandFanSpeed, (uint8_t)value);
        else posted = postValueCommand_(EventId::MicronovaCommandTargetTemperature, (uint8_t)value);
        LOGI("MQTT compact cmd '%s' -> value=%d post=%d", buf, value, posted ? 1 : 0);
        return posted;
    }
    LOGW("MQTT compact cmd unsupported payload='%s'", buf);
    return false;
}

void MicronovaMqttBridgeModule::onInbound_(const MqttInboundMessage& message)
{
    if (!eventBus_ || !message.topic || !message.payload || message.payload[0] == '\0') return;
    LOGI("MQTT cmd rx topic='%s' payload='%s'", message.topic, message.payload);

    char topic[Limits::Mqtt::Buffers::Topic] = {0};
    if (mqttSvc_ && mqttSvc_->formatTopic) {
        mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandCompact, topic, sizeof(topic));
    }
    if (topic[0] != '\0' && strcmp(message.topic, topic) == 0) {
        const bool ok = parseCompact_(message.payload);
        if (!ok) LOGW("MQTT compact cmd failed payload='%s'", message.payload);
        return;
    }

    auto handleAuxOutput = [&](const char* label) {
        bool on = false;
        if (parsePower_(message.payload, on)) {
            const bool ok = setAuxOutput_(on);
            LOGI("MQTT cmd aux_output(%s) on=%d io_ok=%d", label, on ? 1 : 0, ok ? 1 : 0);
        } else {
            LOGW("MQTT cmd aux_output(%s) invalid payload='%s'", label, message.payload);
        }
    };

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandAuxOutput, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        handleAuxOutput("micronova/io/output/d00/set");
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandAuxOutputAlias, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        handleAuxOutput("micronova/command/aux_output/set");
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandAuxOutputRuntime, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        handleAuxOutput("rt/io/output/d00/set");
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPower, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        bool on = false;
        if (parsePower_(message.payload, on)) {
            MicronovaCommandPowerPayload p{(uint8_t)(on ? 1U : 0U)};
            const bool posted = eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
            LOGI("MQTT cmd power on=%d post=%d", on ? 1 : 0, posted ? 1 : 0);
        } else {
            LOGW("MQTT cmd power invalid payload='%s'", message.payload);
        }
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPowerPlus, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = eventBus_->post(EventId::MicronovaCommandPowerPlus, nullptr, 0, moduleId());
        LOGI("MQTT cmd power_plus post=%d", posted ? 1 : 0);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPowerMinus, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = eventBus_->post(EventId::MicronovaCommandPowerMinus, nullptr, 0, moduleId());
        LOGI("MQTT cmd power_minus post=%d", posted ? 1 : 0);
        return;
    }

    bool ok = false;
    const int value = parseInt_(message.payload, ok);
    if (!ok) {
        if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandRefresh, topic, sizeof(topic));
        if (strcmp(message.topic, topic) == 0) {
            const bool posted = eventBus_->post(EventId::MicronovaCommandRefresh, nullptr, 0, moduleId());
            LOGI("MQTT cmd refresh post=%d", posted ? 1 : 0);
            return;
        }
        if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandSweepReadAll, topic, sizeof(topic));
        if (strcmp(message.topic, topic) == 0) {
            const bool posted = eventBus_->post(EventId::MicronovaCommandSweepReadAll, nullptr, 0, moduleId());
            LOGI("MQTT cmd read_all_sweep post=%d", posted ? 1 : 0);
            return;
        } else {
            LOGW("MQTT cmd parse int failed topic='%s' payload='%s'", message.topic, message.payload);
        }
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPowerLevel, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = postValueCommand_(EventId::MicronovaCommandPowerLevel, (uint8_t)value);
        LOGI("MQTT cmd power_level value=%d post=%d", value, posted ? 1 : 0);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandFan, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = postValueCommand_(EventId::MicronovaCommandFanSpeed, (uint8_t)value);
        LOGI("MQTT cmd fan_speed value=%d post=%d", value, posted ? 1 : 0);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandTemperature, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = postValueCommand_(EventId::MicronovaCommandTargetTemperature, (uint8_t)value);
        LOGI("MQTT cmd target_temperature value=%d post=%d", value, posted ? 1 : 0);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandRefresh, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = eventBus_->post(EventId::MicronovaCommandRefresh, nullptr, 0, moduleId());
        LOGI("MQTT cmd refresh(value payload) post=%d", posted ? 1 : 0);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandSweepReadAll, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        const bool posted = eventBus_->post(EventId::MicronovaCommandSweepReadAll, nullptr, 0, moduleId());
        LOGI("MQTT cmd read_all_sweep(value payload) post=%d", posted ? 1 : 0);
        return;
    }
    LOGW("MQTT cmd topic not handled topic='%s' payload='%s'", message.topic, message.payload);
}
