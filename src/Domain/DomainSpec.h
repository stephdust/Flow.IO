#pragma once

#include "Domain/DomainTypes.h"

struct DomainSpec {
    const char* name;
    const DomainIoBinding* ioBindings;
    uint8_t ioBindingCount;
    const PoolDevicePreset* poolDevices;
    uint8_t poolDeviceCount;
    const DomainSensorPreset* sensors;
    uint8_t sensorCount;
    const PoolLogicDefaultsSpec* poolLogicDefaults;
    void (*configurationHook)(AppContext&);
};

