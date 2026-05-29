/**
 * @file Ws2812StatusLedDriver.cpp
 * @brief Implementation for single-LED WS2812 status output.
 */

#include "Modules/HMIModule/Drivers/Ws2812StatusLedDriver.h"

#include <Arduino.h>
#include <esp32-hal-rgb-led.h>
#include <string.h>

void Ws2812StatusLedDriver::setConfig(const Config& cfg)
{
    cfg_ = cfg;
    ready_ = false;
    lastWriteValid_ = false;
}

bool Ws2812StatusLedDriver::begin()
{
    if (!cfg_.enabled || cfg_.gpio < 0) {
        ready_ = false;
        return false;
    }
    if (!digitalPinIsValid((uint8_t)cfg_.gpio)) {
        ready_ = false;
        return false;
    }

    pinMode((uint8_t)cfg_.gpio, OUTPUT);
    blinkPhaseOn_ = true;
    blinkPhaseSinceMs_ = millis();
    ready_ = true;
    applyOutput_(true);
    return true;
}

void Ws2812StatusLedDriver::tick(uint32_t nowMs)
{
    if (!ready_) return;

    if (!state_.enabled || !state_.blinkEnabled) {
        if (!blinkPhaseOn_) {
            blinkPhaseOn_ = true;
            blinkPhaseSinceMs_ = nowMs;
            applyOutput_(true);
        } else {
            applyOutput_(false);
        }
        return;
    }

    const uint16_t phaseMs = blinkPhaseOn_ ? state_.blinkOnMs : state_.blinkOffMs;
    if ((uint32_t)(nowMs - blinkPhaseSinceMs_) >= (uint32_t)phaseMs) {
        blinkPhaseOn_ = !blinkPhaseOn_;
        blinkPhaseSinceMs_ = nowMs;
        applyOutput_(true);
    } else {
        applyOutput_(false);
    }
}

bool Ws2812StatusLedDriver::setState(const Ws2812StatusLedState& state)
{
    Ws2812StatusLedState normalized = state;
    sanitizeState_(normalized);

    const bool changed = memcmp(&state_, &normalized, sizeof(Ws2812StatusLedState)) != 0;
    state_ = normalized;
    if (state_.blinkEnabled) {
        blinkPhaseOn_ = true;
        blinkPhaseSinceMs_ = millis();
    }
    if (changed) applyOutput_(true);
    return true;
}

bool Ws2812StatusLedDriver::getState(Ws2812StatusLedState& out) const
{
    out = state_;
    return true;
}

bool Ws2812StatusLedDriver::setEnabled(bool enabled)
{
    if (state_.enabled == enabled) return true;
    state_.enabled = enabled;
    applyOutput_(true);
    return true;
}

bool Ws2812StatusLedDriver::setColor(uint8_t red, uint8_t green, uint8_t blue)
{
    if (state_.red == red && state_.green == green && state_.blue == blue) return true;
    state_.red = red;
    state_.green = green;
    state_.blue = blue;
    applyOutput_(true);
    return true;
}

bool Ws2812StatusLedDriver::setBrightness(uint8_t brightness)
{
    if (state_.brightness == brightness) return true;
    state_.brightness = brightness;
    applyOutput_(true);
    return true;
}

bool Ws2812StatusLedDriver::setBlink(bool enabled, uint16_t onMs, uint16_t offMs)
{
    if (enabled) {
        if (onMs == 0U) onMs = 250U;
        if (offMs == 0U) offMs = 250U;
    }

    if (state_.blinkEnabled == enabled &&
        state_.blinkOnMs == onMs &&
        state_.blinkOffMs == offMs) {
        return true;
    }

    state_.blinkEnabled = enabled;
    state_.blinkOnMs = onMs;
    state_.blinkOffMs = offMs;
    blinkPhaseOn_ = true;
    blinkPhaseSinceMs_ = millis();
    applyOutput_(true);
    return true;
}

void Ws2812StatusLedDriver::sanitizeState_(Ws2812StatusLedState& state) const
{
    if (!state.blinkEnabled) return;
    if (state.blinkOnMs == 0U) state.blinkOnMs = 250U;
    if (state.blinkOffMs == 0U) state.blinkOffMs = 250U;
}

void Ws2812StatusLedDriver::applyOutput_(bool force)
{
    if (!ready_) return;

    uint8_t outR = 0U;
    uint8_t outG = 0U;
    uint8_t outB = 0U;
    const bool outputEnabled = state_.enabled && (!state_.blinkEnabled || blinkPhaseOn_);
    if (outputEnabled) {
        outR = (uint8_t)(((uint16_t)state_.red * (uint16_t)state_.brightness + 127U) / 255U);
        outG = (uint8_t)(((uint16_t)state_.green * (uint16_t)state_.brightness + 127U) / 255U);
        outB = (uint8_t)(((uint16_t)state_.blue * (uint16_t)state_.brightness + 127U) / 255U);
    }

    if (!force &&
        lastWriteValid_ &&
        lastWriteR_ == outR &&
        lastWriteG_ == outG &&
        lastWriteB_ == outB) {
        return;
    }

    // This board's status LED expects RGB ordering while Arduino's helper
    // internally applies GRB packing, so swap R/G at call-site.
    neopixelWrite((uint8_t)cfg_.gpio, outG, outR, outB);
    lastWriteR_ = outR;
    lastWriteG_ = outG;
    lastWriteB_ = outB;
    lastWriteValid_ = true;
}
