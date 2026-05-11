#pragma once

#include "Board/BoardSpec.h"
#include "Core/Module.h"
#include "Core/ConfigTypes.h"
#include "Modules/Micronova/MicronovaBusModule/MicronovaProtocol.h"

#include <HardwareSerial.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>

struct MicronovaRawValue {
    uint8_t readCode = 0;
    uint8_t memoryAddress = 0;
    int16_t value = 0;
    bool valid = false;
};

struct MicronovaCommand {
    uint8_t code = 0;
    uint8_t address = 0;
    uint8_t value = 0;
    bool write = false;
    uint8_t repeatCount = 1;
    uint8_t repeatRemaining = 1;
    uint16_t repeatDelayMs = MicronovaProtocol::DefaultRepeatDelayMs;
    uint32_t lastSendMs = 0;
    uint8_t txMode = 0;
};

enum class MicronovaWriteTxMode : uint8_t {
    Default = 0,
    BulkNoInterbytePacing = 1,
};

enum class MicronovaBusState : uint8_t {
    Idle,
    Sending,
    WaitTurnaround,
    WaitingReply,
    RepeatDelay
};

class MicronovaBusModule : public Module {
public:
    explicit MicronovaBusModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::MicronovaBus; }
    const char* taskName() const override { return "micronova.bus"; }
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint16_t taskStackSize() const override { return 3072; }

    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::MicronovaBusStartDelayMs; }

    bool begin();
    void tick(uint32_t nowMs);

    bool queueRead(uint8_t readCode, uint8_t address);
    bool queueWrite(uint8_t writeCode,
                    uint8_t address,
                    uint8_t value,
                    uint8_t repeatCount = 1,
                    uint16_t repeatDelayMs = MicronovaProtocol::DefaultRepeatDelayMs,
                    MicronovaWriteTxMode txMode = MicronovaWriteTxMode::Default);
    void clearReadQueue();

    bool pollValue(MicronovaRawValue& out);

    bool isOnline() const { return online_; }
    uint32_t lastResponseMs() const { return lastResponseMs_; }
    bool isIdle() const;
    bool hasPendingCommands() const;
    void clearWriteQueue();
    void clearAllQueues();

private:
    static constexpr uint8_t ReadQueueCapacity = 40;
    static constexpr uint8_t WriteQueueCapacity = 8;
    static constexpr uint8_t ValueQueueCapacity = 32;

    template <typename T, uint8_t N>
    struct Ring {
        T items[N]{};
        uint8_t head = 0;
        uint8_t tail = 0;
        uint8_t count = 0;
    };

    bool pushCommand_(Ring<MicronovaCommand, ReadQueueCapacity>& q, const MicronovaCommand& cmd);
    bool popRead_(MicronovaCommand& out);
    bool pushWrite_(const MicronovaCommand& cmd);
    bool popWrite_(MicronovaCommand& out);
    bool pushValue_(const MicronovaRawValue& value);
    void applyBoardDefaults_(const BoardSpec& board);
    void sendCurrent_(uint32_t nowMs);
    void finishCurrent_();
    void setEnableRx_(bool receive);
    void updateOnline_(bool online);
    uint16_t replyTimeoutMs_() const;
    uint16_t turnaroundDelayMs_() const;
    uint16_t repeatDelayMs_() const;

    HardwareSerial serial_;
    bool begun_ = false;
    bool online_ = false;
    uint32_t lastResponseMs_ = 0;
    uint32_t stateTsMs_ = 0;
    uint32_t offlineTimeoutMs_ = 0;
    uint8_t replyBuf_[8] = {0};
    uint8_t replyLen_ = 0;
    MicronovaBusState state_ = MicronovaBusState::Idle;
    MicronovaCommand current_{};

    Ring<MicronovaCommand, ReadQueueCapacity> readQ_{};
    Ring<MicronovaCommand, WriteQueueCapacity> writeQ_{};
    Ring<MicronovaRawValue, ValueQueueCapacity> valueQ_{};
    mutable portMUX_TYPE queueMux_ = portMUX_INITIALIZER_UNLOCKED;

    int32_t rxPin_ = -1;
    int32_t txPin_ = -1;
    int32_t enableRxPin_ = -1;
    bool enableRxActiveLow_ = true;
    int32_t baudrate_ = MicronovaProtocol::DefaultBaudrate;
    int32_t replyTimeoutMsCfg_ = MicronovaProtocol::DefaultReplyTimeoutMs;
    int32_t turnaroundDelayMsCfg_ = MicronovaProtocol::DefaultTurnaroundDelayMs;
    int32_t repeatDelayMsCfg_ = MicronovaProtocol::DefaultRepeatDelayMs;
    int32_t offlineTimeoutMsCfg_ = 600000;

    ConfigVariable<int32_t,0> rxPinVar_;
    ConfigVariable<int32_t,0> txPinVar_;
    ConfigVariable<int32_t,0> enableRxPinVar_;
    ConfigVariable<bool,0> enableRxActiveLowVar_;
    ConfigVariable<int32_t,0> baudrateVar_;
    ConfigVariable<int32_t,0> replyTimeoutVar_;
    ConfigVariable<int32_t,0> turnaroundDelayVar_;
    ConfigVariable<int32_t,0> repeatDelayVar_;
    ConfigVariable<int32_t,0> offlineTimeoutVar_;
};
