#include "Profiles/FlowIOS3/FlowIOS3Profile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace FlowIOS3 {

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "FlowIOS3",
        &BoardCatalog::activeBoard(),
        &DomainCatalog::pool(),
        {
            "Flow.io",
            "flowio-s3",
            FirmwareVersion::Full,
            "rt"
        },
        nullptr,
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace FlowIOS3
}  // namespace Profiles
