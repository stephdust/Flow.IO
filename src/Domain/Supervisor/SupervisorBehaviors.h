#pragma once

#include "Domain/Supervisor/SupervisorDomain.h"

namespace SupervisorBehaviors {

inline const DomainIoBinding* bindingByRole(DomainRole role)
{
    for (uint8_t i = 0; i < SupervisorDomain::kSupervisorDomain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = SupervisorDomain::kSupervisorDomain.ioBindings[i];
        if (binding.role == role) return &binding;
    }
    return nullptr;
}

}  // namespace SupervisorBehaviors

