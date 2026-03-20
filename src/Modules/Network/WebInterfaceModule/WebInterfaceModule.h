#pragma once
/**
 * @file WebInterfaceModule.h
 * @brief Web interface module for Supervisor profile.
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Core/Services/ILogger.h"
#include <HardwareSerial.h>
#include <ESPAsyncWebServer.h>
#include <freertos/queue.h>
#include "Core/EventBus/EventBus.h"

struct BoardSpec;

class WebInterfaceModule : public Module {
public:
    explicit WebInterfaceModule(const BoardSpec& board);

    const char* moduleId() const override { return "webinterface"; }
    const char* taskName() const override { return "webinterface"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 4096; }

    uint8_t dependencyCount() const override { return 6; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "wifi";
        if (i == 2) return "eventbus";
        if (i == 3) return "datastore";
        if (i == 4) return "cmd";
        if (i == 5) return "i2ccfg.client";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr int kServerPort = 80;

    // Keep UART framing aligned with core log entry limits.
    static constexpr size_t kSerialLogLineChars =
        (size_t)LOG_MSG_MAX + (size_t)LOG_MODULE_NAME_MAX + 64U;
    static constexpr size_t kLineBufferSize = kSerialLogLineChars * 4U;
    static constexpr size_t kUartRxBufferSize = kLineBufferSize * 2U;

    HardwareSerial& uart_ = Serial2;
    uint32_t uartBaud_ = 115200U;
    int uartRxPin_ = 16;
    int uartTxPin_ = 17;
    AsyncWebServer server_{kServerPort};
    AsyncWebSocket ws_{"/wsserial"};
    AsyncWebSocket wsLog_{"/wslog"};

    const LogHubService* logHub_ = nullptr;
    const LogSinkRegistryService* logSinkReg_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    bool started_ = false;
    bool spiffsReady_ = false;
    volatile bool netReady_ = false;
    volatile bool uartPaused_ = false;
    bool localLogSinkRegistered_ = false;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    ServiceRegistry* services_ = nullptr;

    static constexpr size_t kLocalLogLineMax = 240;
    static constexpr UBaseType_t kLocalLogQueueLen = 64;
    QueueHandle_t localLogQueue_ = nullptr;

    char lineBuf_[kLineBufferSize] = {0};
    size_t lineLen_ = 0;
    uint32_t wsFlowConnectCount_ = 0;
    uint32_t wsFlowDisconnectCount_ = 0;
    uint32_t wsFlowSentCount_ = 0;
    uint32_t wsFlowDropCount_ = 0;
    uint32_t wsFlowPartialCount_ = 0;
    uint32_t wsFlowDiscardCount_ = 0;
    uint32_t wsFlowLastPressureLogMs_ = 0;

    void startServer_();
    void onWsEvent_(AsyncWebSocket* server,
                    AsyncWebSocketClient* client,
                    AwsEventType type,
                    void* arg,
                    uint8_t* data,
                    size_t len);
    void onWsLogEvent_(AsyncWebSocket* server,
                       AsyncWebSocketClient* client,
                       AwsEventType type,
                       void* arg,
                       uint8_t* data,
                       size_t len);
    void handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target);
    bool isWebReachable_() const;
    bool getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const;
    void flushLine_(bool force);
    void flushLocalLogQueue_();
    void logWsFlowPressure_(const char* reason);
    static bool isLogByte_(uint8_t c);
    static char levelChar_(LogLevel lvl);
    static const char* levelColor_(LogLevel lvl);
    static const char* colorReset_();
    static bool isSystemTimeValid_();
    static void formatUptime_(char* out, size_t outSize, uint32_t ms);
    static void formatTimestamp_(WebInterfaceModule* self, const LogEntry& e, char* out, size_t outSize);
    static void onLocalLogSinkWrite_(void* ctx, const LogEntry& e);
    static bool svcSetPaused_(void* ctx, bool paused);
    static bool svcIsPaused_(void* ctx);
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
};
