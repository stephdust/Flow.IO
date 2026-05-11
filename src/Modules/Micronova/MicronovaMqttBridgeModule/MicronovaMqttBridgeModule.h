#pragma once

#include "Core/Module.h"
#include "Core/Services/IIO.h"
#include "Core/Services/IMqtt.h"

#include <stdint.h>

class MicronovaMqttBridgeModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::MicronovaMqttBridge; }
    const char* taskName() const override { return "micronova.mqtt"; }
    bool hasTask() const override { return false; }

    uint8_t dependencyCount() const override { return 5; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::DataStore;
        if (i == 3) return ModuleId::Mqtt;
        if (i == 4) return ModuleId::Io;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override {}

private:
    enum MessageId : uint16_t {
        MsgConnection = 1,
        MsgState,
        MsgOnoff,
        MsgPowerState,
        MsgPowerLevel,
        MsgStoveState,
        MsgStoveStateCode,
        MsgAlarmCode,
        MsgLastCommand,
        MsgRoomTemperature,
        MsgFumesTemperature,
        MsgWaterTemperature,
        MsgWaterPressure,
        MsgPowerSensor,
        MsgFanSensor,
        MsgTargetTemperature,
        MsgDisplayLine1,
        MsgDisplayLine2,
        MsgDisplayLine3,
        MsgCount
    };

    static constexpr uint8_t ProducerId = 53;
    static constexpr uint8_t InboundCount = 12;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    static MqttBuildResult buildMessageStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    MqttBuildResult buildMessage_(uint16_t messageId, MqttBuildContext& buildCtx);
    static void onInboundStatic_(void* ctx, const MqttInboundMessage& message);
    void onInbound_(const MqttInboundMessage& message);

    void registerInbound_();
    void enqueueAll_(MqttPublishPriority priority);
    void enqueueForKey_(DataKey key);
    void enqueueForMicronovaKey_(const char* key);
    bool enqueueMsg_(uint16_t msgId, MqttPublishPriority priority);
    bool publishText_(MqttBuildContext& buildCtx, const char* suffix, const char* text, bool retain);
    bool publishInt_(MqttBuildContext& buildCtx, const char* suffix, int32_t value, bool retain);
    bool publishFloat_(MqttBuildContext& buildCtx, const char* suffix, float value, bool retain);
    bool postValueCommand_(EventId eventId, uint8_t value);
    bool setAuxOutput_(bool on);
    bool parseCompact_(const char* payload);
    bool parsePower_(const char* payload, bool& out);
    int parseInt_(const char* payload, bool& ok);

    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    bool producerRegistered_ = false;
    bool inboundRegistered_ = false;
    bool topicsLogged_ = false;

    MqttPublishProducer producer_{};
    MqttInboundHandler inbound_[InboundCount]{};
};
