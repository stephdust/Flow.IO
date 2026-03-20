#include "Domain/DomainCatalog.h"

#include "App/BuildFlags.h"
#include "Domain/Pool/PoolDomain.h"
#include "Domain/Supervisor/SupervisorDomain.h"

namespace DomainCatalog {

const DomainSpec& pool()
{
    return PoolDomain::kPoolDomain;
}

const DomainSpec& supervisor()
{
    return SupervisorDomain::kSupervisorDomain;
}

const DomainSpec& activeDomain()
{
#if FLOW_BUILD_IS_FLOWIO
    return pool();
#else
    return supervisor();
#endif
}

}  // namespace DomainCatalog
