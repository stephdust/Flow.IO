#pragma once

#include "Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h"

namespace PoolDeviceSlots {

static_assert(POOL_DEVICE_MAX == 8, "PoolDevice fixed slot descriptors must match POOL_DEVICE_MAX");

inline constexpr PoolDeviceSlotDescriptor kSlots[POOL_DEVICE_MAX] = {
    {"pd0", "pdm/pd0", "pdmrt/pd0", "pd0en", "pd0dp", "pd0flh", "pd0tc", "pd0ti", "pd0mu", "pd0rt"},
    {"pd1", "pdm/pd1", "pdmrt/pd1", "pd1en", "pd1dp", "pd1flh", "pd1tc", "pd1ti", "pd1mu", "pd1rt"},
    {"pd2", "pdm/pd2", "pdmrt/pd2", "pd2en", "pd2dp", "pd2flh", "pd2tc", "pd2ti", "pd2mu", "pd2rt"},
    {"pd3", "pdm/pd3", "pdmrt/pd3", "pd3en", "pd3dp", "pd3flh", "pd3tc", "pd3ti", "pd3mu", "pd3rt"},
    {"pd4", "pdm/pd4", "pdmrt/pd4", "pd4en", "pd4dp", "pd4flh", "pd4tc", "pd4ti", "pd4mu", "pd4rt"},
    {"pd5", "pdm/pd5", "pdmrt/pd5", "pd5en", "pd5dp", "pd5flh", "pd5tc", "pd5ti", "pd5mu", "pd5rt"},
    {"pd6", "pdm/pd6", "pdmrt/pd6", "pd6en", "pd6dp", "pd6flh", "pd6tc", "pd6ti", "pd6mu", "pd6rt"},
    {"pd7", "pdm/pd7", "pdmrt/pd7", "pd7en", "pd7dp", "pd7flh", "pd7tc", "pd7ti", "pd7mu", "pd7rt"},
};

}  // namespace PoolDeviceSlots
