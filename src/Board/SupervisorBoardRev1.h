#pragma once

#include "Board/BoardSpec.h"

namespace BoardProfiles {

inline constexpr UartSpec kSupervisorBoardRev1Uarts[] = {
    {"log", 0, -1, -1, 115200, true},
    {"bridge", 2, 16, 17, 115200, false},
    {"panel", 2, 33, 32, 115200, false},
};

inline constexpr I2cBusSpec kSupervisorBoardRev1I2c[] = {
    {"interlink", 21, 22, 100000},
};

inline constexpr SupervisorBoardSpec kSupervisorBoardRev1Supervisor{
    {
        240,
        320,
        1,
        0,
        33,
        14,
        15,
        4,
        5,
        19,
        18,
        true,
        false,
        40000000U,
        80
    },
    {
        36,
        120,
        true,
        23,
        40
    },
    {
        25,
        26,
        13,
        115200U
    }
};

inline constexpr BoardSpec kSupervisorBoardRev1{
    "SupervisorBoardRev1",
    kSupervisorBoardRev1Uarts,
    (uint8_t)(sizeof(kSupervisorBoardRev1Uarts) / sizeof(kSupervisorBoardRev1Uarts[0])),
    kSupervisorBoardRev1I2c,
    (uint8_t)(sizeof(kSupervisorBoardRev1I2c) / sizeof(kSupervisorBoardRev1I2c[0])),
    nullptr,
    0,
    nullptr,
    0,
    &kSupervisorBoardRev1Supervisor
};

}  // namespace BoardProfiles
