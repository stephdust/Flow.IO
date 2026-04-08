#pragma once

#include "Board/BoardSpec.h"

namespace BoardProfiles {

inline constexpr uint32_t kSupervisorBoardRev1UartBaud = 115200U;
inline constexpr uint32_t kSupervisorBoardRev1InterlinkI2cHz = 100000U;

inline constexpr UartSpec kSupervisorBoardRev1Uarts[] = {
    {"log", 0, -1, -1, kSupervisorBoardRev1UartBaud, true},
    {"bridge", 2, 16, 17, kSupervisorBoardRev1UartBaud, false},
    {"panel", 2, 33, 32, kSupervisorBoardRev1UartBaud, false},
};

inline constexpr I2cBusSpec kSupervisorBoardRev1I2c[] = {
    {"interlink", 27, 13, kSupervisorBoardRev1InterlinkI2cHz},
};

inline constexpr St7789DisplaySpec kSupervisorBoardRev1Display{
    240,
    320,
    1,
    0,
    0,
    14,
    15,
    4,
    5,
    35,
    18,
    19,
    false,
    true,
    8000000U,
    80
};

inline constexpr SupervisorInputSpec kSupervisorBoardRev1Inputs{
    36,
    120,
    true,
    23,
    40
};

inline constexpr SupervisorUpdateSpec kSupervisorBoardRev1Update{
    25,
    26,
    13,
    kSupervisorBoardRev1UartBaud
};

inline constexpr SupervisorBoardSpec kSupervisorBoardRev1Supervisor{
    kSupervisorBoardRev1Display,
    kSupervisorBoardRev1Inputs,
    kSupervisorBoardRev1Update
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
