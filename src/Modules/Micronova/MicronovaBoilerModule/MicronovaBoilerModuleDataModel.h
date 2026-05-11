#pragma once

#include <stdint.h>

struct MicronovaRuntimeData {
    bool online = false;
    int32_t stoveStateCode = -1;
    char stoveStateText[32] = "";
    char displayLine1[8] = "";
    char displayLine2[8] = "";
    char displayLine3[8] = "";
    char powerState[8] = "UNKNOWN";
    int32_t powerLevel = -1;
    int32_t fanSpeed = -1;
    int32_t targetTemperature = -1;
    float roomTemperature = 0.0f;
    float fumesTemperature = 0.0f;
    float waterTemperature = 0.0f;
    float waterPressure = 0.0f;
    int32_t alarmCode = -1;
    uint32_t lastUpdateMs = 0;
    char lastCommand[24] = "";
};

// MODULE_DATA_MODEL: MicronovaRuntimeData micronova
