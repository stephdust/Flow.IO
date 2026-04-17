#pragma once
/**
 * @file NextionDriver.h
 * @brief Nextion HMI driver implementation.
 */

#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"

#include <Arduino.h>

struct NextionDriverConfig {
    HardwareSerial* serial = &Serial2;
    int8_t rxPin = 16;
    int8_t txPin = 17;
    uint32_t baud = 115200;
    uint32_t minRenderGapMs = 120;
    const char* waterTempTextObject = "tWaterTemp";
    const char* airTempTextObject = "tAirTemp";
    const char* phTextObject = "tpH";
    const char* orpTextObject = "tORP";
    const char* timeTextObject = "tTime";
    const char* dateTextObject = "tDate";
    const char* phGaugePercentObject = "vapHPercent";
    const char* orpGaugePercentObject = "vaOrpPercent";
    const char* stateBitsObject = "globals.vaStates";
    const char* alarmBitsObject = "globals.vaAlarms";
    uint8_t configPageId = 10U;
};

class NextionDriver final : public IHmiDriver {
public:
    NextionDriver() = default;

    void setConfig(const NextionDriverConfig& cfg) { cfg_ = cfg; }

    const char* driverId() const override { return "nextion"; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool pollEvent(HmiEvent& out) override;
    bool publishHomeText(HmiHomeTextField field, const char* text) override;
    bool publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent) override;
    bool publishHomeStateBits(uint32_t stateBits) override;
    bool publishHomeAlarmBits(uint32_t alarmBits) override;
    bool readRtc(HmiRtcDateTime& out, uint16_t timeoutMs) override;
    bool writeRtc(const HmiRtcDateTime& value) override;
    bool renderConfigMenu(const ConfigMenuView& view) override;
    bool refreshConfigMenuValues(const ConfigMenuView& view) override;

private:
    static constexpr uint8_t CustomRxBufSize = 64;
    static constexpr size_t TxBufSize = 160U;

    NextionDriverConfig cfg_{};
    bool started_ = false;
    bool pageReady_ = false;
    uint32_t lastRenderMs_ = 0;

    bool customFrameActive_ = false;
    uint8_t customExpectedLen_ = 0;
    uint8_t customLen_ = 0;
    uint8_t customBuf_[CustomRxBufSize]{};

    bool parseCustomFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out);

    bool sendCmd_(const char* cmd);
    bool sendCmdFmt_(const char* fmt, ...);
    bool sendNum_(const char* objectName, uint32_t value);
    bool sendText_(const char* objectName, const char* value);
    bool readNumber_(const char* expr, uint32_t& value, uint16_t timeoutMs);
    bool readNumberResponse_(uint32_t& value, uint16_t timeoutMs);
    const char* homeTextObjectName_(HmiHomeTextField field) const;
    const char* homeGaugeObjectName_(HmiHomeGaugeField field) const;
    void sanitizeText_(char* out, size_t outLen, const char* in) const;
};
