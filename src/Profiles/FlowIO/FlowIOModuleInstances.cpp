#include "Profiles/FlowIO/FlowIOProfile.h"

#include "Board/BoardCatalog.h"

namespace Profiles {
namespace FlowIO {

ModuleInstances::ModuleInstances(const BoardSpec& board)
    : wifiModule(board),
      i2cCfgServerModule(board),
      ioModule(board)
{
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::activeBoard()};
    return instances;
}

}  // namespace FlowIO
}  // namespace Profiles
