#pragma once
/**
 * @file PoolIoMap.h
 * @brief Shared static mapping between pool-device slots and IO digital outputs.
 */

#include <stdint.h>
#include "Core/Services/IIO.h"

struct PoolIoBinding {
    /** Stable pool-device slot used by PoolDeviceService/pooldevice.write. */
    uint8_t slot = 0;
    /** IOServiceV2 digital output id bound to this slot. */
    IoId ioId = IO_ID_INVALID;
    /** Home Assistant switch object suffix (without fioXX_ prefix). */
    const char* haObjectSuffix = nullptr;
    /** Human-readable name used in HA and setup defaults. */
    const char* name = nullptr;
    /** Optional HA icon. */
    const char* haIcon = nullptr;
};

static constexpr uint8_t POOL_IO_SLOT_FILTRATION_PUMP = 0;
static constexpr uint8_t POOL_IO_SLOT_PH_PUMP = 1;
static constexpr uint8_t POOL_IO_SLOT_CHLORINE_PUMP = 2;
static constexpr uint8_t POOL_IO_SLOT_ROBOT = 3;
static constexpr uint8_t POOL_IO_SLOT_FILL_PUMP = 4;
static constexpr uint8_t POOL_IO_SLOT_CHLORINE_GENERATOR = 5;
static constexpr uint8_t POOL_IO_SLOT_LIGHTS = 6;
static constexpr uint8_t POOL_IO_SLOT_WATER_HEATER = 7;

static constexpr PoolIoBinding FLOW_POOL_IO_BINDINGS[] = {
    {POOL_IO_SLOT_FILTRATION_PUMP,   (IoId)(IO_ID_DO_BASE + 0), "io_flt_pmp",  "Filtration Pump",    "mdi:pool"},
    {POOL_IO_SLOT_PH_PUMP,           (IoId)(IO_ID_DO_BASE + 1), "io_ph_pmp",   "pH Pump",            "mdi:beaker-outline"},
    {POOL_IO_SLOT_CHLORINE_PUMP,     (IoId)(IO_ID_DO_BASE + 2), "io_chl_pmp",  "Chlorine Pump",      "mdi:water-outline"},
    {POOL_IO_SLOT_CHLORINE_GENERATOR,(IoId)(IO_ID_DO_BASE + 3), "io_chl_gen",  "Chlorine Generator", "mdi:flash"},
    {POOL_IO_SLOT_ROBOT,             (IoId)(IO_ID_DO_BASE + 4), "io_robot",    "Robot",              "mdi:robot-vacuum"},
    {POOL_IO_SLOT_LIGHTS,            (IoId)(IO_ID_DO_BASE + 5), "io_lights",   "Lights",             "mdi:lightbulb"},
    {POOL_IO_SLOT_FILL_PUMP,         (IoId)(IO_ID_DO_BASE + 6), "io_fill_pmp", "Fill Pump",          "mdi:water-plus"},
    {POOL_IO_SLOT_WATER_HEATER,      (IoId)(IO_ID_DO_BASE + 7), "io_wat_htr",  "Water Heater",       "mdi:water-boiler"},
};

static constexpr uint8_t FLOW_POOL_IO_BINDING_COUNT =
    (uint8_t)(sizeof(FLOW_POOL_IO_BINDINGS) / sizeof(FLOW_POOL_IO_BINDINGS[0]));

static inline const PoolIoBinding* flowPoolIoBindingBySlot(uint8_t slot)
{
    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        if (FLOW_POOL_IO_BINDINGS[i].slot == slot) return &FLOW_POOL_IO_BINDINGS[i];
    }
    return nullptr;
}

static inline const PoolIoBinding* flowPoolIoBindingByIoId(IoId ioId)
{
    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        if (FLOW_POOL_IO_BINDINGS[i].ioId == ioId) return &FLOW_POOL_IO_BINDINGS[i];
    }
    return nullptr;
}
