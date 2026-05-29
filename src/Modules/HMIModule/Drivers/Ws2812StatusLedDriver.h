#pragma once
/**
 * @file Ws2812StatusLedDriver.h
 * @brief Single-LED WS2812 status driver (color/brightness/blink).
 */

#include <stdint.h>

struct Ws2812StatusLedState {
    bool enabled = true;
    bool blinkEnabled = false;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 255;
    uint8_t brightness = 96;
    uint16_t blinkOnMs = 250;
    uint16_t blinkOffMs = 250;
};

class Ws2812StatusLedDriver {
public:
    struct Config {
        int8_t gpio = -1;
        bool enabled = false;
    };

    void setConfig(const Config& cfg);
    bool begin();
    void tick(uint32_t nowMs);

    bool setState(const Ws2812StatusLedState& state);
    bool getState(Ws2812StatusLedState& out) const;
    bool setEnabled(bool enabled);
    bool setColor(uint8_t red, uint8_t green, uint8_t blue);
    bool setBrightness(uint8_t brightness);
    bool setBlink(bool enabled, uint16_t onMs, uint16_t offMs);

    bool isReady() const { return ready_; }
    bool isConfigured() const { return cfg_.enabled && cfg_.gpio >= 0; }

private:
    void sanitizeState_(Ws2812StatusLedState& state) const;
    void applyOutput_(bool force);

    Config cfg_{};
    Ws2812StatusLedState state_{};
    bool ready_ = false;
    bool blinkPhaseOn_ = true;
    uint32_t blinkPhaseSinceMs_ = 0U;
    bool lastWriteValid_ = false;
    uint8_t lastWriteR_ = 0U;
    uint8_t lastWriteG_ = 0U;
    uint8_t lastWriteB_ = 0U;
};

