#pragma once
/**
 * @file IMqtt.h
 * @brief MQTT service interface.
 */

#include <stddef.h>
#include <stdint.h>

enum class MqttPublishPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2,
};

/** @brief Service wrapper for publishing and topic formatting via MQTTModule. */
struct MqttService {
    bool (*publish)(void* ctx, const char* topic, const char* payload, int qos, bool retain);
    bool (*publishPrio)(void* ctx, const char* topic, const char* payload, int qos, bool retain, uint8_t prio);
    void (*formatTopic)(void* ctx, const char* suffix, char* out, size_t outLen);
    bool (*isConnected)(void* ctx);
    void* ctx;
};
