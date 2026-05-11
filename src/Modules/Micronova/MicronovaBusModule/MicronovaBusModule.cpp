#include "Modules/Micronova/MicronovaBusModule/MicronovaBusModule.h"

#include <Arduino.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaBusModule)
#include "Core/ModuleLog.h"

namespace {

#ifndef MICRONOVA_UART_WRITE_BYTE_PACING
#define MICRONOVA_UART_WRITE_BYTE_PACING 1
#endif

#ifndef MICRONOVA_UART_WRITE_INTERBYTE_DELAY_MS
#define MICRONOVA_UART_WRITE_INTERBYTE_DELAY_MS 1
#endif

uint8_t micronovaUartIndex_(const BoardSpec& board)
{
    const UartSpec* spec = boardFindUart(board, "micronova");
    return spec ? spec->uartIndex : 1U;
}

}  // namespace

MicronovaBusModule::MicronovaBusModule(const BoardSpec& board)
    : serial_(micronovaUartIndex_(board)),
      rxPinVar_{NVS_KEY("mn_rx"), "rx_pin", "micronova/uart", ConfigType::Int32, &rxPin_, ConfigPersistence::Persistent, 0},
      txPinVar_{NVS_KEY("mn_tx"), "tx_pin", "micronova/uart", ConfigType::Int32, &txPin_, ConfigPersistence::Persistent, 0},
      enableRxPinVar_{NVS_KEY("mn_enrx"), "enable_rx_pin", "micronova/uart", ConfigType::Int32, &enableRxPin_, ConfigPersistence::Persistent, 0},
      enableRxActiveLowVar_{NVS_KEY("mn_enrx_lo"), "enable_rx_active_low", "micronova/uart", ConfigType::Bool, &enableRxActiveLow_, ConfigPersistence::Persistent, 0},
      baudrateVar_{NVS_KEY("mn_baud"), "baudrate", "micronova/serial", ConfigType::Int32, &baudrate_, ConfigPersistence::Persistent, 0},
      replyTimeoutVar_{NVS_KEY("mn_rpto"), "reply_timeout_ms", "micronova/serial", ConfigType::Int32, &replyTimeoutMsCfg_, ConfigPersistence::Persistent, 0},
      turnaroundDelayVar_{NVS_KEY("mn_turn"), "turnaround_delay_ms", "micronova/serial", ConfigType::Int32, &turnaroundDelayMsCfg_, ConfigPersistence::Persistent, 0},
      repeatDelayVar_{NVS_KEY("mn_rep"), "repeat_delay_ms", "micronova/serial", ConfigType::Int32, &repeatDelayMsCfg_, ConfigPersistence::Persistent, 0},
      offlineTimeoutVar_{NVS_KEY("mn_offto"), "offline_timeout_ms", "micronova/serial", ConfigType::Int32, &offlineTimeoutMsCfg_, ConfigPersistence::Persistent, 0}
{
    applyBoardDefaults_(board);
}

void MicronovaBusModule::applyBoardDefaults_(const BoardSpec& board)
{
    const UartSpec* spec = boardFindUart(board, "micronova");
    if (!spec) return;
    rxPin_ = spec->rxPin;
    txPin_ = spec->txPin;
    enableRxPin_ = spec->enableRxPin;
    baudrate_ = (int32_t)spec->baud;
}

void MicronovaBusModule::init(ConfigStore& cfg, ServiceRegistry&)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Micronova;
    cfg.registerVar(rxPinVar_, kCfgModuleId, 1);
    cfg.registerVar(txPinVar_, kCfgModuleId, 1);
    cfg.registerVar(enableRxPinVar_, kCfgModuleId, 1);
    cfg.registerVar(enableRxActiveLowVar_, kCfgModuleId, 1);
    cfg.registerVar(baudrateVar_, kCfgModuleId, 2);
    cfg.registerVar(replyTimeoutVar_, kCfgModuleId, 2);
    cfg.registerVar(turnaroundDelayVar_, kCfgModuleId, 2);
    cfg.registerVar(repeatDelayVar_, kCfgModuleId, 2);
    cfg.registerVar(offlineTimeoutVar_, kCfgModuleId, 2);
}

void MicronovaBusModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    LOGI("Micronova UART begin deferred");
}

void MicronovaBusModule::onStart(ConfigStore&, ServiceRegistry&)
{
    (void)begin();
}

bool MicronovaBusModule::begin()
{
    if (begun_) {
        serial_.end();
        begun_ = false;
    }

    if (enableRxPin_ >= 0) {
        pinMode((uint8_t)enableRxPin_, OUTPUT);
        setEnableRx_(false);
    }

    const uint32_t baud = (baudrate_ > 0) ? (uint32_t)baudrate_ : MicronovaProtocol::DefaultBaudrate;
    serial_.begin(baud, SERIAL_8N2, rxPin_, txPin_);
    serial_.setTimeout(0);
    while (serial_.available() > 0) {
        (void)serial_.read();
    }

    offlineTimeoutMs_ = (offlineTimeoutMsCfg_ > 0) ? (uint32_t)offlineTimeoutMsCfg_ : 600000UL;
    state_ = MicronovaBusState::Idle;
    replyLen_ = 0;
    begun_ = true;
    LOGI("Micronova UART begin baud=%lu rx=%ld tx=%ld en_rx=%ld",
         (unsigned long)baud,
         (long)rxPin_,
         (long)txPin_,
         (long)enableRxPin_);
    LOGI("Micronova UART EN_RX polarity active_low=%d", enableRxActiveLow_ ? 1 : 0);
    return true;
}

void MicronovaBusModule::loop()
{
    tick(millis());
}

bool MicronovaBusModule::pushCommand_(Ring<MicronovaCommand, ReadQueueCapacity>& q, const MicronovaCommand& cmd)
{
    bool ok = false;
    portENTER_CRITICAL(&queueMux_);
    if (q.count < ReadQueueCapacity) {
        q.items[q.tail] = cmd;
        q.tail = (uint8_t)((q.tail + 1U) % ReadQueueCapacity);
        ++q.count;
        ok = true;
    }
    portEXIT_CRITICAL(&queueMux_);
    return ok;
}

bool MicronovaBusModule::popRead_(MicronovaCommand& out)
{
    bool ok = false;
    portENTER_CRITICAL(&queueMux_);
    if (readQ_.count > 0U) {
        out = readQ_.items[readQ_.head];
        readQ_.head = (uint8_t)((readQ_.head + 1U) % ReadQueueCapacity);
        --readQ_.count;
        ok = true;
    }
    portEXIT_CRITICAL(&queueMux_);
    return ok;
}

bool MicronovaBusModule::pushWrite_(const MicronovaCommand& cmd)
{
    bool ok = false;
    portENTER_CRITICAL(&queueMux_);
    if (writeQ_.count < WriteQueueCapacity) {
        writeQ_.items[writeQ_.tail] = cmd;
        writeQ_.tail = (uint8_t)((writeQ_.tail + 1U) % WriteQueueCapacity);
        ++writeQ_.count;
        ok = true;
    }
    portEXIT_CRITICAL(&queueMux_);
    return ok;
}

bool MicronovaBusModule::popWrite_(MicronovaCommand& out)
{
    bool ok = false;
    portENTER_CRITICAL(&queueMux_);
    if (writeQ_.count > 0U) {
        out = writeQ_.items[writeQ_.head];
        writeQ_.head = (uint8_t)((writeQ_.head + 1U) % WriteQueueCapacity);
        --writeQ_.count;
        ok = true;
    }
    portEXIT_CRITICAL(&queueMux_);
    return ok;
}

bool MicronovaBusModule::pushValue_(const MicronovaRawValue& value)
{
    portENTER_CRITICAL(&queueMux_);
    if (valueQ_.count >= ValueQueueCapacity) {
        valueQ_.head = (uint8_t)((valueQ_.head + 1U) % ValueQueueCapacity);
        --valueQ_.count;
    }
    valueQ_.items[valueQ_.tail] = value;
    valueQ_.tail = (uint8_t)((valueQ_.tail + 1U) % ValueQueueCapacity);
    ++valueQ_.count;
    portEXIT_CRITICAL(&queueMux_);
    return true;
}

bool MicronovaBusModule::queueRead(uint8_t readCode, uint8_t address)
{
    MicronovaCommand cmd{};
    cmd.code = readCode;
    cmd.address = address;
    cmd.write = false;
    cmd.repeatCount = 1;
    cmd.repeatRemaining = 1;
    return pushCommand_(readQ_, cmd);
}

bool MicronovaBusModule::queueWrite(uint8_t writeCode,
                                    uint8_t address,
                                    uint8_t value,
                                    uint8_t repeatCount,
                                    uint16_t repeatDelayMs,
                                    MicronovaWriteTxMode txMode)
{
    MicronovaCommand cmd{};
    cmd.code = writeCode;
    cmd.address = address;
    cmd.value = value;
    cmd.write = true;
    cmd.repeatCount = repeatCount == 0 ? 1 : repeatCount;
    cmd.repeatRemaining = cmd.repeatCount;
    cmd.repeatDelayMs = repeatDelayMs == 0 ? repeatDelayMs_() : repeatDelayMs;
    cmd.txMode = (uint8_t)txMode;
    return pushWrite_(cmd);
}

void MicronovaBusModule::clearReadQueue()
{
    portENTER_CRITICAL(&queueMux_);
    readQ_.head = 0U;
    readQ_.tail = 0U;
    readQ_.count = 0U;
    portEXIT_CRITICAL(&queueMux_);
}

void MicronovaBusModule::clearWriteQueue()
{
    portENTER_CRITICAL(&queueMux_);
    writeQ_.head = 0U;
    writeQ_.tail = 0U;
    writeQ_.count = 0U;
    portEXIT_CRITICAL(&queueMux_);
}

void MicronovaBusModule::clearAllQueues()
{
    portENTER_CRITICAL(&queueMux_);
    readQ_.head = 0U;
    readQ_.tail = 0U;
    readQ_.count = 0U;
    writeQ_.head = 0U;
    writeQ_.tail = 0U;
    writeQ_.count = 0U;
    valueQ_.head = 0U;
    valueQ_.tail = 0U;
    valueQ_.count = 0U;
    portEXIT_CRITICAL(&queueMux_);
}

bool MicronovaBusModule::isIdle() const
{
    bool idle = false;
    portENTER_CRITICAL(&queueMux_);
    idle = (state_ == MicronovaBusState::Idle);
    portEXIT_CRITICAL(&queueMux_);
    return idle;
}

bool MicronovaBusModule::hasPendingCommands() const
{
    bool pending = false;
    portENTER_CRITICAL(&queueMux_);
    pending = (readQ_.count > 0U) || (writeQ_.count > 0U) || (state_ != MicronovaBusState::Idle);
    portEXIT_CRITICAL(&queueMux_);
    return pending;
}

bool MicronovaBusModule::pollValue(MicronovaRawValue& out)
{
    bool ok = false;
    portENTER_CRITICAL(&queueMux_);
    if (valueQ_.count > 0U) {
        out = valueQ_.items[valueQ_.head];
        valueQ_.head = (uint8_t)((valueQ_.head + 1U) % ValueQueueCapacity);
        --valueQ_.count;
        ok = true;
    }
    portEXIT_CRITICAL(&queueMux_);
    return ok;
}

void MicronovaBusModule::setEnableRx_(bool receive)
{
    if (enableRxPin_ < 0) return;
    const uint8_t level = enableRxActiveLow_
        ? (receive ? LOW : HIGH)
        : (receive ? HIGH : LOW);
    digitalWrite((uint8_t)enableRxPin_, level);
}

void MicronovaBusModule::sendCurrent_(uint32_t nowMs)
{
    if (!begun_) return;
    while (serial_.available() > 0) {
        (void)serial_.read();
    }
    replyLen_ = 0;
    setEnableRx_(false);

    if (current_.write) {
        const uint8_t checksum = MicronovaProtocol::writeChecksum(current_.code, current_.address, current_.value);
        const uint8_t bytes[4] = {
            current_.code,
            current_.address,
            current_.value,
            checksum
        };
        LOGI("Micronova UART TX write code=0x%02X addr=0x%02X value=0x%02X chk=0x%02X repeat_left=%u/%u delay_ms=%u",
             current_.code,
             current_.address,
             current_.value,
             checksum,
             (unsigned)current_.repeatRemaining,
             (unsigned)current_.repeatCount,
             (unsigned)current_.repeatDelayMs);
        if (current_.txMode == (uint8_t)MicronovaWriteTxMode::BulkNoInterbytePacing) {
            (void)serial_.write(bytes, sizeof(bytes));
            serial_.flush();
        } else {
#if MICRONOVA_UART_WRITE_BYTE_PACING
            for (uint8_t i = 0; i < sizeof(bytes); ++i) {
                (void)serial_.write(bytes[i]);
                serial_.flush();
                if (i + 1U < sizeof(bytes) && MICRONOVA_UART_WRITE_INTERBYTE_DELAY_MS > 0) {
                    delay(MICRONOVA_UART_WRITE_INTERBYTE_DELAY_MS);
                }
            }
#else
            (void)serial_.write(bytes, sizeof(bytes));
            serial_.flush();
#endif
        }
        current_.lastSendMs = nowMs;
        if (current_.repeatRemaining > 0) --current_.repeatRemaining;
        state_ = current_.repeatRemaining > 0 ? MicronovaBusState::RepeatDelay : MicronovaBusState::Idle;
        stateTsMs_ = nowMs;
        if (state_ == MicronovaBusState::Idle) finishCurrent_();
        return;
    }

    const uint8_t bytes[2] = {current_.code, current_.address};
    LOGD("Micronova UART TX read code=0x%02X addr=0x%02X", current_.code, current_.address);
    (void)serial_.write(bytes, sizeof(bytes));
    serial_.flush();
    current_.lastSendMs = nowMs;
    setEnableRx_(true);
    state_ = MicronovaBusState::WaitTurnaround;
    stateTsMs_ = nowMs;
}

void MicronovaBusModule::finishCurrent_()
{
    current_ = MicronovaCommand{};
    state_ = MicronovaBusState::Idle;
    stateTsMs_ = millis();
    setEnableRx_(false);
}

void MicronovaBusModule::updateOnline_(bool online)
{
    if (online_ == online) return;
    online_ = online;
    LOGI("Micronova bus %s", online ? "online" : "offline");
}

uint16_t MicronovaBusModule::replyTimeoutMs_() const
{
    return replyTimeoutMsCfg_ > 0 ? (uint16_t)replyTimeoutMsCfg_ : MicronovaProtocol::DefaultReplyTimeoutMs;
}

uint16_t MicronovaBusModule::turnaroundDelayMs_() const
{
    return turnaroundDelayMsCfg_ > 0 ? (uint16_t)turnaroundDelayMsCfg_ : MicronovaProtocol::DefaultTurnaroundDelayMs;
}

uint16_t MicronovaBusModule::repeatDelayMs_() const
{
    return repeatDelayMsCfg_ > 0 ? (uint16_t)repeatDelayMsCfg_ : MicronovaProtocol::DefaultRepeatDelayMs;
}

void MicronovaBusModule::tick(uint32_t nowMs)
{
    if (!begun_) return;

    if (online_ && lastResponseMs_ != 0U && offlineTimeoutMs_ > 0U &&
        (uint32_t)(nowMs - lastResponseMs_) > offlineTimeoutMs_) {
        updateOnline_(false);
    }

    switch (state_) {
        case MicronovaBusState::Idle:
            if (popWrite_(current_) || popRead_(current_)) {
                state_ = MicronovaBusState::Sending;
            }
            break;

        case MicronovaBusState::Sending:
            sendCurrent_(nowMs);
            break;

        case MicronovaBusState::WaitTurnaround:
            if ((uint32_t)(nowMs - stateTsMs_) >= turnaroundDelayMs_()) {
                state_ = MicronovaBusState::WaitingReply;
                stateTsMs_ = nowMs;
            }
            break;

        case MicronovaBusState::WaitingReply:
            while (serial_.available() > 0 && replyLen_ < sizeof(replyBuf_)) {
                const int b = serial_.read();
                if (b >= 0) replyBuf_[replyLen_++] = (uint8_t)b;
            }

            // Some UART adapters/boards can echo request bytes back on RX.
            // Drop leading echo pairs (code,address) and keep waiting for the real reply pair.
            while (replyLen_ >= 2U &&
                   replyBuf_[0] == current_.code &&
                   replyBuf_[1] == current_.address) {
                LOGD("Micronova UART RX echo dropped code=0x%02X addr=0x%02X", current_.code, current_.address);
                if (replyLen_ > 2U) {
                    memmove(replyBuf_, replyBuf_ + 2U, replyLen_ - 2U);
                }
                replyLen_ = (uint8_t)(replyLen_ - 2U);
            }

            if (replyLen_ >= 2U) {
                const bool valid = MicronovaProtocol::responseMatches(current_.code,
                                                                      current_.address,
                                                                      replyBuf_[0],
                                                                      replyBuf_[1]);
                MicronovaRawValue value{};
                value.readCode = current_.code;
                value.memoryAddress = current_.address;
                value.value = replyBuf_[1];
                value.valid = valid;
                if (valid) {
                    LOGD("Micronova UART RX ok req(code=0x%02X addr=0x%02X) resp(code=0x%02X value=0x%02X)",
                         current_.code,
                         current_.address,
                         replyBuf_[0],
                         replyBuf_[1]);
                    lastResponseMs_ = nowMs;
                    updateOnline_(true);
                    (void)pushValue_(value);
                } else {
                    LOGW("Micronova UART RX invalid req(code=0x%02X addr=0x%02X) resp0=0x%02X resp1=0x%02X",
                         current_.code,
                         current_.address,
                         replyBuf_[0],
                         replyBuf_[1]);
                }
                finishCurrent_();
            } else if ((uint32_t)(nowMs - stateTsMs_) >= replyTimeoutMs_()) {
                LOGW("Micronova UART RX timeout code=0x%02X addr=0x%02X waited_ms=%u buffered=%u",
                     current_.code,
                     current_.address,
                     (unsigned)replyTimeoutMs_(),
                     (unsigned)replyLen_);
                updateOnline_(false);
                finishCurrent_();
            }
            break;

        case MicronovaBusState::RepeatDelay:
            if ((uint32_t)(nowMs - current_.lastSendMs) >= current_.repeatDelayMs) {
                state_ = MicronovaBusState::Sending;
            }
            break;
    }
}
