#include "Profiles/FlowIOS3/FlowIOS3Profile.h"

#include "Board/BoardCatalog.h"

namespace Profiles {
namespace FlowIOS3 {

ModuleInstances::ModuleInstances(const BoardSpec& board)
    : ethernetModule(board),
      wifiModule(board),
      webInterfaceModule(board),
      firmwareUpdateModule(board),
      ioModule(board)
{
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::activeBoard()};
    return instances;
}

}  // namespace FlowIOS3
}  // namespace Profiles
