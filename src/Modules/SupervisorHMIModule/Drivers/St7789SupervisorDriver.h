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

class SupervisorSt7789 : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;
    using Adafruit_ST77xx::setColRowStart;
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
    uint8_t flowAlarmActiveCount = 0;
    uint8_t flowAlarmCodeCount = 0;
    char flowAlarmCodes[10][24]{};

    char fwState[16]{};
    char fwTarget[16]{};
    uint8_t fwProgress = 0;
    bool fwBusy = false;
    bool fwPending = false;
    char fwMsg[48]{};

    bool wifiResetPending = false;
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
    int8_t dcPin = 4;
    int8_t rstPin = 5;
    int8_t mosiPin = 19;
    int8_t sclkPin = 18;
    bool swapColorBytes = true;
    bool invertColors = false;
    uint32_t spiHz = 40000000;
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
    SupervisorSt7789 display_;
    bool started_ = false;
    bool layoutDrawn_ = false;
    bool backlightOn_ = false;
    uint32_t lastRenderMs_ = 0;
};
