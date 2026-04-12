#pragma once
/**
 * @file PcntCounterDriver.h
 * @brief ESP32 hardware pulse counter driver backed by PCNT.
 */

#include <Arduino.h>
#include <driver/pcnt.h>
#include <stdint.h>

#include "Modules/IOModule/IODrivers/IODriver.h"

class PcntCounterDriver : public IDigitalCounterDriver {
public:
    PcntCounterDriver(const char* driverId,
                      uint8_t pin,
                      bool activeHigh,
                      uint8_t inputPullMode,
                      uint8_t edgeMode,
                      uint32_t counterDebounceUs);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;

    bool write(bool) override { return false; }
    bool read(bool& on) const override;
    bool readCount(int32_t& count) const override;
    bool readDebugStats(IODigitalCounterDebugStats& out) const override;

private:
    struct RuntimeState {
        int32_t pulseCount = 0;
        int32_t rawPulseCount = 0;
        int16_t lastHardwareCount = 0;
        uint32_t sampleCount = 0;
        uint32_t ignoredDebounceCount = 0;
        uint32_t foldCount = 0;
        uint32_t lastAcceptedMs = 0;
        uint32_t lastSampleMs = 0;
        uint32_t idleSinceMs = 0;
        bool gateArmed = true;
        bool started = false;
    };

    static constexpr int16_t kCounterHighLimit = 32767;
    static constexpr int16_t kCounterLowLimit = -32768;
    static constexpr int16_t kFoldThreshold = 30000;

    static pcnt_unit_t allocUnit_();
    static void releaseUnit_(pcnt_unit_t unit);
    bool syncCounter_(uint32_t nowMs) const;
    uint32_t debounceWindowMs_() const;
    void configureEdgeModes_(pcnt_count_mode_t& posMode, pcnt_count_mode_t& negMode) const;

    const char* driverId_ = nullptr;
    uint8_t pin_ = 0;
    bool activeHigh_ = true;
    uint8_t inputPullMode_ = 0;
    uint8_t edgeMode_ = 1;
    uint32_t counterDebounceUs_ = 0;
    pcnt_unit_t unit_ = PCNT_UNIT_MAX;
    mutable RuntimeState state_{};
};
