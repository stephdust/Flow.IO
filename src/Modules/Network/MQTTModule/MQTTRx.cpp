/**
 * @file MQTTRx.cpp
 * @brief MQTT receive-path parsing and acknowledgements for MQTTModule.
 */

#include "MQTTModule.h"

#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <ArduinoJson.h>
#include <string.h>

void MQTTModule::processRx_(const RxMsg& msg)
{
    if (strcmp(msg.topic, topicCmd_) == 0) {
        processRxCmd_(msg);
        return;
    }

    if (strcmp(msg.topic, topicCfgSet_) == 0) {
        processRxCfgSet_(msg);
        return;
    }

    publishRxError_(MqttTopics::SuffixAck, ErrorCode::UnknownTopic, "rx", false);
}

void MQTTModule::processRxCmd_(const RxMsg& msg)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    DeserializationError err = deserializeJson(doc, msg.payload);
    if (err || !doc.is<JsonObjectConst>()) {
        BufferUsageTracker::note(TrackedBufferId::MqttCmdDoc,
                                 doc.memoryUsage(),
                                 sizeof(doc),
                                 "cmd",
                                 nullptr);
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::BadCmdJson, "cmd", true);
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    JsonVariantConst cmdVar = root["cmd"];
    if (!cmdVar.is<const char*>()) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }

    const char* cmdVal = cmdVar.as<const char*>();
    if (!cmdVal || cmdVal[0] == '\0') {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }
    BufferUsageTracker::note(TrackedBufferId::MqttCmdDoc,
                             doc.memoryUsage(),
                             sizeof(doc),
                             cmdVal,
                             nullptr);

    if (!cmdSvc_ || !cmdSvc_->execute) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::CmdServiceUnavailable, "cmd", false);
        return;
    }

    char cmd[Limits::Mqtt::Buffers::CmdName] = {0};
    size_t cmdLen = strlen(cmdVal);
    if (cmdLen >= sizeof(cmd)) cmdLen = sizeof(cmd) - 1U;
    memcpy(cmd, cmdVal, cmdLen);
    cmd[cmdLen] = '\0';

    const char* argsJson = nullptr;
    char argsBuf[Limits::Mqtt::Buffers::CmdArgs] = {0};
    JsonVariantConst argsVar = root["args"];
    if (!argsVar.isNull()) {
        const size_t written = serializeJson(argsVar, argsBuf, sizeof(argsBuf));
        if (written == 0U || written >= sizeof(argsBuf)) {
            publishRxError_(MqttTopics::SuffixAck, ErrorCode::ArgsTooLarge, "cmd", true);
            return;
        }
        argsJson = argsBuf;
    }

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, cmd, msg.payload, argsJson, replyBuf_, sizeof(replyBuf_));
    if (!ok) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::CmdHandlerFailed, "cmd", false);
        return;
    }
    BufferUsageTracker::note(TrackedBufferId::MqttReplyBuf,
                             strnlen(replyBuf_, sizeof(replyBuf_)),
                             sizeof(replyBuf_),
                             cmd,
                             nullptr);

    const int wrote = snprintf(payloadBuf_, sizeof(payloadBuf_),
                               "{\"ok\":true,\"cmd\":\"%s\",\"reply\":%s}",
                               cmd,
                               replyBuf_);
    if (!(wrote > 0 && (size_t)wrote < sizeof(payloadBuf_))) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::InternalAckOverflow, "cmd", false);
        return;
    }
    BufferUsageTracker::note(TrackedBufferId::MqttPayloadBuf,
                             (size_t)wrote,
                             sizeof(payloadBuf_),
                             cmd,
                             nullptr);

    (void)enqueueAck_(MqttTopics::SuffixAck, payloadBuf_, 0, false, MqttPublishPriority::High);
}

void MQTTModule::processRxCfgSet_(const RxMsg& msg)
{
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::CfgServiceUnavailable, "cfg/set", false);
        return;
    }

    static constexpr size_t CFG_DOC_CAPACITY = Limits::JsonCfgBuf;
    static StaticJsonDocument<CFG_DOC_CAPACITY> cfgDoc;
    cfgDoc.clear();

    const DeserializationError cfgErr = deserializeJson(cfgDoc, msg.payload);
    if (cfgErr || !cfgDoc.is<JsonObjectConst>()) {
        BufferUsageTracker::note(TrackedBufferId::MqttCfgDoc,
                                 cfgDoc.memoryUsage(),
                                 sizeof(cfgDoc),
                                 "cfg/set",
                                 nullptr);
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::BadCfgJson, "cfg/set", true);
        return;
    }
    const char* cfgPeakSource = "cfg/set";
    JsonObjectConst cfgRoot = cfgDoc.as<JsonObjectConst>();
    for (JsonPairConst kv : cfgRoot) {
        cfgPeakSource = kv.key().c_str();
        break;
    }
    BufferUsageTracker::note(TrackedBufferId::MqttCfgDoc,
                             cfgDoc.memoryUsage(),
                             sizeof(cfgDoc),
                             cfgPeakSource,
                             "<json>");

    if (!cfgSvc_->applyJson(cfgSvc_->ctx, msg.payload)) {
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::CfgApplyFailed, "cfg/set", false);
        return;
    }

    if (!writeOkJson(payloadBuf_, sizeof(payloadBuf_), "cfg/set")) {
        snprintf(payloadBuf_, sizeof(payloadBuf_), "{\"ok\":true}");
    }
    BufferUsageTracker::note(TrackedBufferId::MqttPayloadBuf,
                             strnlen(payloadBuf_, sizeof(payloadBuf_)),
                             sizeof(payloadBuf_),
                             "cfg/set",
                             nullptr);
    (void)enqueueAck_(MqttTopics::SuffixCfgAck, payloadBuf_, 1, false, MqttPublishPriority::High);
}

void MQTTModule::publishRxError_(const char* ackTopicSuffix, ErrorCode code, const char* where, bool parseFailure)
{
    if (parseFailure) ++parseFailCount_;
    else ++handlerFailCount_;

    syncRxMetrics_();

    if (!writeErrorJson(payloadBuf_, sizeof(payloadBuf_), code, where)) {
        snprintf(payloadBuf_, sizeof(payloadBuf_), "{\"ok\":false}");
    }
    BufferUsageTracker::note(TrackedBufferId::MqttPayloadBuf,
                             strnlen(payloadBuf_, sizeof(payloadBuf_)),
                             sizeof(payloadBuf_),
                             where,
                             nullptr);

    (void)enqueueAck_(ackTopicSuffix, payloadBuf_, 0, false, MqttPublishPriority::High);
}

void MQTTModule::syncRxMetrics_()
{
    if (!dataStore_) return;
    setMqttRxDrop(*dataStore_, rxDropCount_);
    setMqttOversizeDrop(*dataStore_, oversizeDropCount_);
    setMqttParseFail(*dataStore_, parseFailCount_);
    setMqttHandlerFail(*dataStore_, handlerFailCount_);
}

void MQTTModule::countRxDrop_()
{
    ++rxDropCount_;
    syncRxMetrics_();
}

void MQTTModule::countOversizeDrop_()
{
    ++oversizeDropCount_;
    ++rxDropCount_;
    syncRxMetrics_();
}
