#include "Profiles/Supervisor/SupervisorProfile.h"

#include "Board/BoardCatalog.h"

namespace Profiles {
namespace Supervisor {

ModuleInstances::ModuleInstances(const BoardSpec& board, const SupervisorRuntimeOptions& runtime)
    : webInterfaceModule(board),
      firmwareUpdateModule(board),
      supervisorHMIModule(board, runtime)
{
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::supervisorBoardRev1(), runtimeOptions()};
    return instances;
}

}  // namespace Supervisor
}  // namespace Profiles
