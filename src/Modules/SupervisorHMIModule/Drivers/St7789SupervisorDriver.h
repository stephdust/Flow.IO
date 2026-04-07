#pragma once
/**
 * @file St7789SupervisorDriver.h
 * @brief ST7789 local HMI driver for Supervisor status screen.
 */

#include <stdint.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "Core/Services/INetworkAccess.h"
#include "Core/Services/IWifi.h"

constexpr uint8_t kSupervisorAlarmSlotCount = 4;

class SupervisorSt7789 : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;
    using Adafruit_ST77xx::setColRowStart;
};

enum class SupervisorAlarmState : uint8_t {
    Clear = 0,
    Active = 1,
    Acked = 2,
};

struct SupervisorHmiViewModel {
    bool wifiConnected = false;
    WifiState wifiState = WifiState::Idle;
    NetworkAccessMode accessMode = NetworkAccessMode::None;
    bool netReachable = false;
    char ip[20]{};
    int32_t rssiDbm = -127;
    bool hasRssi = false;

    bool flowCfgReady = false;
    bool flowLinkOk = false;
    char flowFirmware[24]{};
    bool flowHasRssi = false;
    int32_t flowRssiDbm = -127;
    bool flowHasHeapFrag = false;
    uint8_t flowHeapFragPct = 0;
    bool flowMqttReady = false;
    uint32_t flowMqttRxDrop = 0;
    uint32_t flowMqttParseFail = 0;
    uint32_t flowI2cReqCount = 0;
    uint32_t flowI2cBadReqCount = 0;
    uint32_t flowI2cLastReqAgoMs = 0;
    bool flowHasPoolModes = false;
    bool flowFiltrationAuto = false;
    bool flowWinterMode = false;
    bool flowPhAutoMode = false;
    bool flowOrpAutoMode = false;
    bool flowFiltrationOn = false;
    bool flowPhPumpOn = false;
    bool flowChlorinePumpOn = false;
    bool flowHasPh = false;
    float flowPhValue = 0.0f;
    bool flowHasOrp = false;
    float flowOrpValue = 0.0f;
    bool flowHasWaterTemp = false;
    float flowWaterTemp = 0.0f;
    bool flowHasAirTemp = false;
    float flowAirTemp = 0.0f;
    uint8_t flowAlarmActiveCount = 0;
    uint8_t flowAlarmCodeCount = 0;
    char flowAlarmCodes[10][24]{};
    uint8_t flowAlarmActCount = 0;
    uint8_t flowAlarmAckCount = 0;
    uint8_t flowAlarmClrCount = kSupervisorAlarmSlotCount;
    SupervisorAlarmState flowAlarmStates[kSupervisorAlarmSlotCount]{
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
    };

    char fwState[16]{};
    char fwTarget[16]{};
    uint8_t fwProgress = 0;
    bool fwBusy = false;
    bool fwPending = false;
    char fwMsg[48]{};

    bool factoryResetPending = false;
    char banner[48]{};
};

struct St7789SupervisorDriverConfig {
    uint16_t resX = 240;
    uint16_t resY = 320;
    uint8_t rotation = 0;
    int8_t colStart = 0;
    int8_t rowStart = 0;
    int8_t backlightPin = 14;
    int8_t csPin = 15;
    int8_t dcPin = 2;
    int8_t rstPin = 4;
    int8_t misoPin = 35;
    int8_t mosiPin = 18;
    int8_t sclkPin = 19;
    bool swapColorBytes = true;
    bool invertColors = false;
    uint32_t spiHz = 8000000;
    uint16_t minRenderGapMs = 80;
};

class St7789SupervisorDriver {
public:
    explicit St7789SupervisorDriver(const St7789SupervisorDriverConfig& cfg);

    bool begin();
    void setBacklight(bool on);
    bool isBacklightOn() const { return backlightOn_; }
    bool render(const SupervisorHmiViewModel& vm, bool force = false);

private:
    const char* wifiStateText_(WifiState st) const;
    const char* netModeText_(NetworkAccessMode mode) const;

    St7789SupervisorDriverConfig cfg_{};
#if defined(VSPI)
    SPIClass spiBus_{VSPI};
#else
    SPIClass spiBus_{HSPI};
#endif
    SupervisorSt7789 display_;
    bool started_ = false;
    bool layoutDrawn_ = false;
    bool backlightOn_ = false;
    uint32_t lastRenderMs_ = 0;
    uint8_t lastPage_ = 0xFFU;
    char lastTime_[16]{};
    char lastDate_[20]{};
    char lastIp_[20]{};
    bool lastHasRssi_ = false;
    int32_t lastRssiDbm_ = -127;
    bool lastMqttReady_ = false;
    uint8_t lastSystemState_ = 0xFFU;
    bool lastRows_[7]{};
    bool lastHasPh_ = false;
    float lastPhValue_ = 0.0f;
    bool lastHasOrp_ = false;
    float lastOrpValue_ = 0.0f;
    bool lastHasWaterTemp_ = false;
    float lastWaterTemp_ = 0.0f;
    bool lastHasAirTemp_ = false;
    float lastAirTemp_ = 0.0f;
    uint8_t lastAlarmActCount_ = 0xFFU;
    uint8_t lastAlarmAckCount_ = 0xFFU;
    uint8_t lastAlarmClrCount_ = 0xFFU;
    SupervisorAlarmState lastAlarmStates_[kSupervisorAlarmSlotCount]{
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
        SupervisorAlarmState::Clear,
    };
};
