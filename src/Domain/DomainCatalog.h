#pragma once

#include "Domain/DomainSpec.h"

namespace DomainCatalog {

const DomainSpec& pool();
const DomainSpec& supervisor();
const DomainSpec& activeDomain();

}  // namespace DomainCatalog

