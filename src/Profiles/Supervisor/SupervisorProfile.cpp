#include "Profiles/Supervisor/SupervisorProfile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace Supervisor {

const SupervisorRuntimeOptions& runtimeOptions()
{
    static constexpr SupervisorRuntimeOptions kRuntimeOptions{
        10000U,
        3000U
    };
    return kRuntimeOptions;
}

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "Supervisor",
        &BoardCatalog::supervisorBoardRev1(),
        &DomainCatalog::supervisor(),
        {
            "Flow.IO Supervisor",
            "flow-supervisor",
            FirmwareVersion::Full,
            "rt"
        },
        &runtimeOptions(),
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace Supervisor
}  // namespace Profiles
