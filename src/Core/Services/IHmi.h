#pragma once
/**
 * @file IHmi.h
 * @brief HMI service interface.
 */

#include <stddef.h>
#include <stdint.h>

struct HmiStatusLedState {
    bool enabled = true;
    bool blinkEnabled = false;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 255;
    uint8_t brightness = 96;
    uint16_t blinkOnMs = 250;
    uint16_t blinkOffMs = 250;
};

struct HmiService {
    bool (*requestRefresh)(void* ctx);
    bool (*openConfigHome)(void* ctx);
    bool (*openConfigModule)(void* ctx, const char* module);
    bool (*buildConfigMenuJson)(void* ctx, char* out, size_t outLen);
    bool (*setLedPage)(void* ctx, uint8_t page);
    uint8_t (*getLedPage)(void* ctx);
    bool (*setStatusLedState)(void* ctx, const HmiStatusLedState* state);
    bool (*getStatusLedState)(void* ctx, HmiStatusLedState* out);
    bool (*setStatusLedAutoWifiMode)(void* ctx, bool enabled);
    bool (*isStatusLedAutoWifiMode)(void* ctx);
    void* ctx;
};
