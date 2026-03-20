#pragma once

#include <string.h>

#include "Board/BoardTypes.h"

inline const IoPointSpec* boardFindIoPoint(const BoardSpec& board, BoardSignal signal)
{
    for (uint8_t i = 0; i < board.ioPointCount; ++i) {
        if (board.ioPoints[i].signal == signal) return &board.ioPoints[i];
    }
    return nullptr;
}

inline const UartSpec* boardFindUart(const BoardSpec& board, const char* name)
{
    if (!name) return nullptr;
    for (uint8_t i = 0; i < board.uartCount; ++i) {
        const UartSpec& spec = board.uarts[i];
        if (spec.name && strcmp(spec.name, name) == 0) return &spec;
    }
    return nullptr;
}

inline const OneWireBusSpec* boardFindOneWire(const BoardSpec& board, BoardSignal signal)
{
    for (uint8_t i = 0; i < board.oneWireCount; ++i) {
        if (board.oneWireBuses[i].signal == signal) return &board.oneWireBuses[i];
    }
    return nullptr;
}

inline const SupervisorBoardSpec* boardSupervisorConfig(const BoardSpec& board)
{
    return board.supervisor;
}
