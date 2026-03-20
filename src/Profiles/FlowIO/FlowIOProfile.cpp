#include "Profiles/FlowIO/FlowIOProfile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace FlowIO {

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "FlowIO",
        &BoardCatalog::flowIOBoardRev1(),
        &DomainCatalog::pool(),
        {
            "Flow.IO",
            "flowio",
            FirmwareVersion::Full,
            "rt"
        },
        nullptr,
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace FlowIO
}  // namespace Profiles
