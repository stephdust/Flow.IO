#include "Board/BoardCatalog.h"

#include "App/BuildFlags.h"
#include "Board/FlowIOBoardRev1.h"
#include "Board/SupervisorBoardRev1.h"

namespace BoardCatalog {

const BoardSpec& flowIOBoardRev1()
{
    return BoardProfiles::kFlowIOBoardRev1;
}

const BoardSpec& supervisorBoardRev1()
{
    return BoardProfiles::kSupervisorBoardRev1;
}

const BoardSpec& activeBoard()
{
#if FLOW_BUILD_IS_FLOWIO
    return flowIOBoardRev1();
#else
    return supervisorBoardRev1();
#endif
}

}  // namespace BoardCatalog

