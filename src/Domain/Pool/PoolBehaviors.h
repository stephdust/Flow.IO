#pragma once

#include "Domain/Pool/PoolDomain.h"

namespace PoolBehaviors {

inline const DomainIoBinding* bindingByRole(DomainRole role)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = PoolDomain::kPoolDomain.ioBindings[i];
        if (binding.role == role) return &binding;
    }
    return nullptr;
}

inline const DomainIoBinding* bindingBySignal(BoardSignal signal)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = PoolDomain::kPoolDomain.ioBindings[i];
        if (binding.signal == signal) return &binding;
    }
    return nullptr;
}

inline const PoolDevicePreset* poolDeviceByRole(DomainRole role)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = PoolDomain::kPoolDomain.poolDevices[i];
        if (preset.role == role) return &preset;
    }
    return nullptr;
}

inline const DomainSensorPreset* sensorByRole(DomainRole role)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.sensorCount; ++i) {
        const DomainSensorPreset& preset = PoolDomain::kPoolDomain.sensors[i];
        if (preset.role == role) return &preset;
    }
    return nullptr;
}

}  // namespace PoolBehaviors

