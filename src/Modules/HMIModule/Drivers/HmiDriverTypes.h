#pragma once
/**
 * @file HmiDriverTypes.h
 * @brief Driver abstraction for HMI devices.
 */

#include <stdint.h>

#include "Modules/HMIModule/ConfigMenuModel.h"

enum class HmiEventType : uint8_t {
    None = 0,
    Home = 1,
    Back = 2,
    Validate = 3,
    NextPage = 4,
    PrevPage = 5,
    RowActivate = 6,
    RowToggle = 7,
    RowCycle = 8,
    RowSetText = 9,
    RowSetSlider = 10,
    RowEdit = 11,
    Command = 12,
    ConfigEnter = 13,
    ConfigExit = 14
};

enum class HmiCommandId : uint8_t {
    None = 0,
    HomeFiltrationSet = 1,
    HomeAutoModeSet = 2,
    HomeSyncRequest = 3,
    HomeFiltrationToggle = 4,
    HomeAutoModeToggle = 5,
    HomeOrpAutoModeToggle = 6,
    HomePhAutoModeToggle = 7,
    HomeWinterModeToggle = 8,
    HomeLightsToggle = 9,
    HomeRobotToggle = 10,
    HomeConfigOpen = 11,
    HomePhPumpSet = 12,
    HomeOrpPumpSet = 13,
    HomePhPumpToggle = 14,
    HomeOrpPumpToggle = 15,
};

struct HmiEvent {
    HmiEventType type = HmiEventType::None;
    HmiCommandId command = HmiCommandId::None;
    uint8_t row = 0;
    uint8_t value = 0;
    int8_t direction = 1;
    float sliderValue = 0.0f;
    char text[48]{};
};

struct HmiRtcDateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

enum class HmiHomeTextField : uint8_t {
    WaterTemp = 0,
    AirTemp = 1,
    Ph = 2,
    Orp = 3,
    Time = 4,
    Date = 5,
};

enum class HmiHomeGaugeField : uint8_t {
    PhPercent = 0,
    OrpPercent = 1,
};

enum : uint8_t {
    HMI_HOME_STATE_FILTER_PUMP_ON = 0,
    HMI_HOME_STATE_PH_PUMP_ON = 1,
    HMI_HOME_STATE_ORP_PUMP_ON = 2,
    HMI_HOME_STATE_AUTO_MODE = 3,
    HMI_HOME_STATE_PH_AUTO_MODE = 4,
    HMI_HOME_STATE_ORP_AUTO_MODE = 5,
    HMI_HOME_STATE_WINTER_MODE = 6,
    HMI_HOME_STATE_WIFI_READY = 7,
    HMI_HOME_STATE_MQTT_READY = 8,
    HMI_HOME_STATE_ROBOT_ON = 9,
    HMI_HOME_STATE_LIGHTS_ON = 10,
    HMI_HOME_STATE_HEATER_ON = 11,
    HMI_HOME_STATE_FILLING_ON = 12,
};

enum : uint8_t {
    HMI_HOME_ALARM_WATER_LEVEL_LOW = 0,
    HMI_HOME_ALARM_PH_TANK_LOW = 1,
    HMI_HOME_ALARM_CHLORINE_TANK_LOW = 2,
    HMI_HOME_ALARM_PH_PUMP_RUNTIME = 3,
    HMI_HOME_ALARM_ORP_PUMP_RUNTIME = 4,
    HMI_HOME_ALARM_PSI = 5,
};

class IHmiDriver {
public:
    virtual ~IHmiDriver() = default;

    virtual const char* driverId() const = 0;
    virtual bool begin() = 0;
    virtual void tick(uint32_t nowMs) = 0;
    virtual bool pollEvent(HmiEvent& out) = 0;
    virtual bool publishHomeText(HmiHomeTextField field, const char* text) = 0;
    virtual bool publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent) = 0;
    virtual bool publishHomeStateBits(uint32_t stateBits) = 0;
    virtual bool publishHomeAlarmBits(uint32_t alarmBits) = 0;
    virtual bool readRtc(HmiRtcDateTime& out, uint16_t timeoutMs) = 0;
    virtual bool writeRtc(const HmiRtcDateTime& value) = 0;
    virtual bool renderConfigMenu(const ConfigMenuView& view) = 0;
    virtual bool refreshConfigMenuValues(const ConfigMenuView& view) = 0;
};
