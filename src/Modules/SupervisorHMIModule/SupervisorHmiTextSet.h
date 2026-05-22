#pragma once
/**
 * @file SupervisorHmiTextSet.h
 * @brief Built-in localized texts for Supervisor local TFT HMI.
 */

#include <stddef.h>

constexpr size_t kSupervisorHmiAlarmLabelCount = 8U;

struct SupervisorHmiTextSet {
    const char* alarmLabels[kSupervisorHmiAlarmLabelCount];
    const char* stateMqttOff;
    const char* stateFlowUnavailable;
    const char* alarmSummaryTitle;
    const char* measureFallback;
    const char* bannerLocalDisplayReady;
    const char* bannerHoldResetFmt;
    const char* bannerKeepHoldingFmt;
    const char* bannerFactoryResetStarting;
    const char* bannerFactoryResetFailed;
    const char* bannerPirIdleBacklightOff;
};

const SupervisorHmiTextSet* supervisorHmiTextSetDefault();
const SupervisorHmiTextSet* supervisorHmiTextSetForLanguage(const char* lang);
