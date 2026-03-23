/**
 * @file MQTTTransport.cpp
 * @brief Broker transport setup and ESP-IDF client event handling for MQTTModule.
 */

#include "MQTTModule.h"

#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"

#include <esp_heap_caps.h>
#include <string.h>

bool MQTTModule::ensureClient_()
{
    if (client_ && !clientConfigDirty_) return true;

    destroyClient_();

    const int uriLen = snprintf(brokerUri_, sizeof(brokerUri_), "mqtt://%s:%ld", cfgData_.host, (long)cfgData_.port);
    if (!(uriLen > 0 && (size_t)uriLen < sizeof(brokerUri_))) return false;

    esp_mqtt_client_config_t cfg = {};
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    cfg.broker.address.uri = brokerUri_;
    cfg.credentials.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.credentials.username = cfgData_.user;
        cfg.credentials.authentication.password = cfgData_.pass;
    }
    cfg.session.last_will.topic = topicStatus_;
    cfg.session.last_will.msg = "{\"online\":false}";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.disable_auto_reconnect = true;
#else
    cfg.uri = brokerUri_;
    cfg.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.username = cfgData_.user;
        cfg.password = cfgData_.pass;
    }
    cfg.lwt_topic = topicStatus_;
    cfg.lwt_msg = "{\"online\":false}";
    cfg.lwt_qos = 1;
    cfg.lwt_retain = 1;
    cfg.disable_auto_reconnect = true;
    cfg.user_context = this;
#endif

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) return false;

    if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MQTTModule::mqttEventHandlerStatic_, this) != ESP_OK) {
        destroyClient_();
        return false;
    }

    clientConfigDirty_ = false;
    return true;
}

void MQTTModule::stopClient_(bool intentional)
{
    if (!client_) return;
    if (intentional) suppressDisconnectEvent_ = true;
    (void)esp_mqtt_client_stop(client_);
    clientStarted_ = false;
}

void MQTTModule::destroyClient_()
{
    if (!client_) return;
    (void)esp_mqtt_client_stop(client_);
    (void)esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    clientStarted_ = false;
}

void MQTTModule::connectMqtt_()
{
    buildTopics_();
    suppressDisconnectEvent_ = false;

    if (!ensureClient_()) {
        setState_(MQTTState::ErrorWait);
        return;
    }

    esp_err_t err = ESP_FAIL;
    if (!clientStarted_) {
        err = esp_mqtt_client_start(client_);
        if (err == ESP_OK) clientStarted_ = true;
    } else {
        err = esp_mqtt_client_reconnect(client_);
    }

    if (err != ESP_OK) {
        setState_(MQTTState::ErrorWait);
        return;
    }

    setState_(MQTTState::Connecting);
}

void MQTTModule::onConnect_(bool)
{
    suppressDisconnectEvent_ = false;

    (void)esp_mqtt_client_subscribe(client_, topicCmd_, 0);
    (void)esp_mqtt_client_subscribe(client_, topicCfgSet_, 1);

    retryCount_ = 0;
    retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;
    setState_(MQTTState::Connected);

    (void)enqueue(ProducerIdStatus, StatusMsgOnline, MqttPublishPriority::High, 0);
    if (cfgProducer_) {
        cfgProducer_->requestFullSync(MqttPublishPriority::Low);
    }
    runtimeProducerCore_.onConnected();
    enqueueAlarmFullSync_();

    const uint32_t now = millis();
    for (uint8_t i = 0; i < runtimePublisherCount_; ++i) {
        RuntimePublisher& p = runtimePublishers_[i];
        if (!p.used || p.periodMs == 0U) continue;
        p.nextDueMs = now;
        (void)enqueue(ProducerIdStatus, p.messageId, MqttPublishPriority::Normal, 0);
    }
}

void MQTTModule::onDisconnect_(const esp_mqtt_error_codes_t*)
{
    if (suppressDisconnectEvent_) {
        suppressDisconnectEvent_ = false;
        return;
    }

    setState_(MQTTState::ErrorWait);
}

void MQTTModule::onMessage_(const char* topic,
                            size_t topicLen,
                            const char* payload,
                            size_t len,
                            size_t index,
                            size_t total)
{
    if (!rxQ_) return;
    if (!topic || !payload) {
        countRxDrop_();
        return;
    }
    if (index != 0 || len != total) {
        countRxDrop_();
        return;
    }

    if (topicLen >= sizeof(RxMsg{}.topic) || len >= sizeof(RxMsg{}.payload)) {
        countOversizeDrop_();
        return;
    }

    RxMsg msg{};
    memcpy(msg.topic, topic, topicLen);
    msg.topic[topicLen] = '\0';
    memcpy(msg.payload, payload, len);
    msg.payload[len] = '\0';

    if (xQueueSend(rxQ_, &msg, 0) != pdTRUE) {
        countRxDrop_();
        return;
    }

    const UBaseType_t queued = uxQueueMessagesWaiting(rxQ_);
    BufferUsageTracker::note(TrackedBufferId::MqttRxQueueStorage,
                             (size_t)queued * sizeof(RxMsg),
                             (size_t)Limits::Mqtt::Capacity::RxQueueLen * sizeof(RxMsg),
                             msg.topic,
                             nullptr);
}

void MQTTModule::mqttEventHandlerStatic_(void* handler_args,
                                         esp_event_base_t,
                                         int32_t event_id,
                                         void* event_data)
{
    MQTTModule* self = static_cast<MQTTModule*>(handler_args);
    if (!self || !event_data) return;

    esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            self->onConnect_(ev->session_present != 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnect_(ev ? ev->error_handle : nullptr);
            break;
        case MQTT_EVENT_DATA:
            self->onMessage_(ev->topic,
                             (size_t)ev->topic_len,
                             ev->data,
                             (size_t)ev->data_len,
                             (size_t)ev->current_data_offset,
                             (size_t)ev->total_data_len);
            break;
        default:
            break;
    }
}

void MQTTModule::formatTopic(char* out, size_t outLen, const char* suffix) const
{
    if (!out || outLen == 0 || !suffix) return;
    snprintf(out, outLen, "%s/%s/%s", cfgData_.baseTopic, deviceId_, suffix);
}
