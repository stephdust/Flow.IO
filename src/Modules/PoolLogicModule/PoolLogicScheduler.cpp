/**
 * @file PoolLogicScheduler.cpp
 * @brief Scheduler and filtration window logic for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/PoolLogicModule/FiltrationWindow.h"

#include <cstring>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

void PoolLogicModule::ensureDailySlot_()
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("time.scheduler service unavailable");
        return;
    }

    // The daily recompute slot is the long-lived scheduler anchor; it does not
    // directly change outputs, it only asks the loop to rebuild the window.
    TimeSchedulerSlot recalc{};
    recalc.slot = SLOT_DAILY_RECALC;
    recalc.eventId = POOLLOGIC_EVENT_DAILY_RECALC;
    recalc.enabled = true;
    recalc.hasEnd = false;
    recalc.replayStartOnBoot = false;
    recalc.mode = TimeSchedulerMode::RecurringClock;
    recalc.weekdayMask = TIME_WEEKDAY_ALL;
    recalc.startHour = PoolDefaults::FiltrationPivotHour;
    recalc.startMinute = 0;
    recalc.endHour = 0;
    recalc.endMinute = 0;
    recalc.startEpochSec = 0;
    recalc.endEpochSec = 0;
    strncpy(recalc.label, "poollogic_daily_recalc", sizeof(recalc.label) - 1);
    recalc.label[sizeof(recalc.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &recalc)) {
        LOGW("Failed to set scheduler slot %u", (unsigned)SLOT_DAILY_RECALC);
    }
}

bool PoolLogicModule::computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut)
{
    // The actual deterministic formula stays isolated in FiltrationWindow.cpp;
    // this method only adapts module config into that pure helper.
    FiltrationWindowInput in{};
    in.waterTemp = waterTemp;
    in.lowThreshold = waterTempLowThreshold_;
    in.setpoint = waterTempSetpoint_;
    in.startMinHour = filtrationStartMin_;
    in.stopMaxHour = filtrationStopMax_;

    FiltrationWindowOutput out{};
    if (!computeFiltrationWindowDeterministic(in, out)) return false;
    startHourOut = out.startHour;
    stopHourOut = out.stopHour;
    durationOut = out.durationHours;
    return true;
}

bool PoolLogicModule::recalcAndApplyFiltrationWindow_(uint8_t* startHourOut,
                                                      uint8_t* stopHourOut,
                                                      uint8_t* durationOut)
{
    if (!ioSvc_ || !ioSvc_->readAnalog) {
        LOGW("No IOServiceV2 available for water temperature");
        return false;
    }
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    float waterTemp = 0.0f;
    if (!loadAnalogSensor_(waterTempIoId_, waterTemp)) {
        LOGW("Water temperature unavailable on ioId=%u", (unsigned)waterTempIoId_);
        return false;
    }

    uint8_t startHour = 0;
    uint8_t stopHour = 0;
    uint8_t duration = 0;
    if (!computeFiltrationWindow_(waterTemp, startHour, stopHour, duration)) {
        LOGW("Invalid water temperature value");
        return false;
    }

    // PoolLogic stores the computed window back into the shared scheduler so
    // filtration state changes continue to arrive as regular scheduler events.
    TimeSchedulerSlot window{};
    window.slot = SLOT_FILTR_WINDOW;
    window.eventId = POOLLOGIC_EVENT_FILTRATION_WINDOW;
    window.enabled = true;
    window.hasEnd = true;
    window.replayStartOnBoot = true;
    window.mode = TimeSchedulerMode::RecurringClock;
    window.weekdayMask = TIME_WEEKDAY_ALL;
    window.startHour = startHour;
    window.startMinute = 0;
    window.endHour = stopHour;
    window.endMinute = 0;
    window.startEpochSec = 0;
    window.endEpochSec = 0;
    strncpy(window.label, "poollogic_filtration", sizeof(window.label) - 1);
    window.label[sizeof(window.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &window)) {
        LOGW("Failed to set filtration window slot=%u", (unsigned)SLOT_FILTR_WINDOW);
        return false;
    }

    if (cfgStore_) {
        (void)cfgStore_->set(calcStartVar_, startHour);
        (void)cfgStore_->set(calcStopVar_, stopHour);
    } else {
        filtrationCalcStart_ = startHour;
        filtrationCalcStop_ = stopHour;
    }
    if (startHourOut) *startHourOut = startHour;
    if (stopHourOut) *stopHourOut = stopHour;
    if (durationOut) *durationOut = duration;

    LOGI("Filtration duration=%uh water=%.2fC start=%uh stop=%uh",
         (unsigned)duration,
         (double)waterTemp,
         (unsigned)startHour,
         (unsigned)stopHour);
    return true;
}
