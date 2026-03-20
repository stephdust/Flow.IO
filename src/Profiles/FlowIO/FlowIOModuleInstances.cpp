#include "Profiles/FlowIO/FlowIOProfile.h"

namespace Profiles {
namespace FlowIO {

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{};
    return instances;
}

}  // namespace FlowIO
}  // namespace Profiles

