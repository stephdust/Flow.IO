#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Board {
namespace SerialMap {

#ifndef FLOW_SWAP_LOG_HMI_SERIAL
#define FLOW_SWAP_LOG_HMI_SERIAL 0
#endif

static constexpr bool SwapLogAndHmi = (FLOW_SWAP_LOG_HMI_SERIAL != 0);

static constexpr uint32_t LogBaud = 115200;
static constexpr uint32_t HmiBaud = 115200;

static constexpr int8_t NoPin = -1;
static constexpr int8_t Uart2Rx = 16;
static constexpr int8_t Uart2Tx = 17;

inline Stream& logSerial()
{
    return SwapLogAndHmi ? static_cast<Stream&>(Serial2) : static_cast<Stream&>(Serial);
}

inline void beginLogSerial()
{
    if (SwapLogAndHmi) {
        Serial2.begin(LogBaud, SERIAL_8N1, Uart2Rx, Uart2Tx);
    } else {
        Serial.begin(LogBaud);
    }
}

inline HardwareSerial& hmiSerial()
{
    if constexpr (SwapLogAndHmi) {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
        return Serial2;
#else
        return Serial;
#endif
    } else {
        return Serial2;
    }
}

inline int8_t logRxPin()
{
    return SwapLogAndHmi ? Uart2Rx : NoPin;
}

inline int8_t logTxPin()
{
    return SwapLogAndHmi ? Uart2Tx : NoPin;
}

inline int8_t hmiRxPin()
{
    return SwapLogAndHmi ? NoPin : Uart2Rx;
}

inline int8_t hmiTxPin()
{
    return SwapLogAndHmi ? NoPin : Uart2Tx;
}

inline uint32_t uart0Baud()
{
    return SwapLogAndHmi ? HmiBaud : LogBaud;
}

}  // namespace SerialMap
}  // namespace Board
