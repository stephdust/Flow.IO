#pragma once
/**
 * @file I2CCfgClientModuleDataModel.h
 * @brief Supervisor-side mirror of Flow runtime values.
 */

#include <stdint.h>

#include "Core/RuntimeUi.h"

constexpr uint8_t kFlowRemoteDashboardSlotCount = 8U;

struct FlowRemoteDashboardSlotRuntime {
    bool enabled = false;
    RuntimeUiId runtimeUiId = 0U;
    char label[24]{};
    uint16_t bgColor565 = 0U;
    bool available = false;
    uint8_t wireType = (uint8_t)RuntimeUiWireType::NotFound;
    bool boolValue = false;
    int32_t i32Value = 0;
    uint32_t u32Value = 0U;
    float f32Value = 0.0f;
    uint8_t enumValue = 0U;
};

struct FlowRemoteRuntimeData {
    bool ready = false;
    bool linkOk = false;
    char firmware[24]{};
    bool hasRssi = false;
    int32_t rssiDbm = -127;
    bool hasHeapFrag = false;
    uint8_t heapFragPct = 0;
    bool mqttReady = false;
    uint32_t mqttRxDrop = 0;
    uint32_t mqttParseFail = 0;
    uint32_t i2cReqCount = 0;
    uint32_t i2cBadReqCount = 0;
    uint32_t i2cLastReqAgoMs = 0;
    bool hasPoolModes = false;
    bool filtrationAuto = false;
    bool winterMode = false;
    bool phAutoMode = false;
    bool orpAutoMode = false;
    bool filtrationOn = false;
    bool phPumpOn = false;
    bool chlorinePumpOn = false;
    bool hasPh = false;
    float phValue = 0.0f;
    bool hasOrp = false;
    float orpValue = 0.0f;
    bool hasWaterTemp = false;
    float waterTemp = 0.0f;
    bool hasAirTemp = false;
    float airTemp = 0.0f;
    bool hasWaterCounter = false;
    float waterCounter = 0.0f;
    bool hasPsi = false;
    float psi = 0.0f;
    bool hasBmp280Temp = false;
    float bmp280Temp = 0.0f;
    bool hasBme680Temp = false;
    float bme680Temp = 0.0f;
    uint32_t alarmActiveMask = 0U;
    uint32_t alarmResettableMask = 0U;
    uint32_t alarmConditionMask = 0U;
    FlowRemoteDashboardSlotRuntime dashboardSlots[kFlowRemoteDashboardSlotCount]{};
};

// MODULE_DATA_MODEL: FlowRemoteRuntimeData flowRemote
