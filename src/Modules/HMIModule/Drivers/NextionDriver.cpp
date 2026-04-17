/**
 * @file NextionDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/Drivers/NextionDriver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr uint8_t NEXTION_FF = 0xFF;
static constexpr uint8_t NEXTION_RSP_NUMBER = 0x71;
static constexpr uint8_t NEXTION_CUSTOM_START = '#';
static constexpr uint8_t NEXTION_CMD_PAGE = 0x50;
static constexpr uint8_t NEXTION_CMD_NAV = 0x51;
static constexpr uint8_t NEXTION_CMD_ROW_ACTIVATE = 0x52;
static constexpr uint8_t NEXTION_CMD_ROW_TOGGLE = 0x53;
static constexpr uint8_t NEXTION_CMD_ROW_CYCLE = 0x54;
static constexpr uint8_t NEXTION_CMD_ROW_EDIT = 0x55;
static constexpr uint8_t NEXTION_CMD_HOME_ACTION = 0x60;

static constexpr uint8_t NAV_HOME = 1;
static constexpr uint8_t NAV_BACK = 2;
static constexpr uint8_t NAV_VALIDATE = 3;
static constexpr uint8_t NAV_NEXT = 4;
static constexpr uint8_t NAV_PREV = 5;
static constexpr uint8_t NAV_CONFIG_EXIT = 6;
static constexpr uint8_t HOME_ACTION_FILTRATION = 1;
static constexpr uint8_t HOME_ACTION_AUTO_MODE = 2;
static constexpr uint8_t HOME_ACTION_SYNC = 3;
static constexpr uint8_t HOME_ACTION_CONFIG_OPEN = 4;
static constexpr uint8_t HOME_ACTION_PH_PUMP = 5;
static constexpr uint8_t HOME_ACTION_ORP_PUMP = 6;
static constexpr uint8_t HOME_ACTION_PH_PUMP_TOGGLE = 7;
static constexpr uint8_t HOME_ACTION_ORP_PUMP_TOGGLE = 8;
static constexpr uint8_t HOME_ACTION_FILTRATION_TOGGLE = 9;
static constexpr uint8_t HOME_ACTION_AUTO_MODE_TOGGLE = 10;
static constexpr uint8_t HOME_ACTION_ORP_AUTO_MODE_TOGGLE = 11;
static constexpr uint8_t HOME_ACTION_PH_AUTO_MODE_TOGGLE = 12;
static constexpr uint8_t HOME_ACTION_WINTER_MODE_TOGGLE = 13;
static constexpr uint8_t HOME_ACTION_LIGHTS_TOGGLE = 14;
static constexpr uint8_t HOME_ACTION_ROBOT_TOGGLE = 15;

} // namespace

bool NextionDriver::begin()
{
    if (started_) return true;
    if (!cfg_.serial) return false;

    if (cfg_.rxPin >= 0 && cfg_.txPin >= 0) {
        cfg_.serial->begin(cfg_.baud, SERIAL_8N1, cfg_.rxPin, cfg_.txPin);
    } else {
        cfg_.serial->begin(cfg_.baud);
    }
    delay(30);
    started_ = true;
    pageReady_ = false;
    lastRenderMs_ = 0;
    customFrameActive_ = false;
    customExpectedLen_ = 0;
    customLen_ = 0;
    return true;
}

void NextionDriver::tick(uint32_t)
{
}

bool NextionDriver::sendCmd_(const char* cmd)
{
    if (!started_ || !cfg_.serial || !cmd) return false;
    cfg_.serial->print(cmd);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    return true;
}

bool NextionDriver::sendCmdFmt_(const char* fmt, ...)
{
    if (!fmt) return false;
    char cmd[TxBufSize]{};
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return sendCmd_(cmd);
}

bool NextionDriver::sendNum_(const char* objectName, uint32_t value)
{
    if (!objectName || objectName[0] == '\0') return false;
    return sendCmdFmt_("%s.val=%lu", objectName, (unsigned long)value);
}

void NextionDriver::sanitizeText_(char* out, size_t outLen, const char* in) const
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0' && pos + 1 < outLen; ++i) {
        const unsigned char uc = (unsigned char)in[i];
        if (uc < 32U) continue;
        const char c = (char)uc;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) break;
            out[pos++] = '\\';
            out[pos++] = c;
            continue;
        }
        out[pos++] = c;
    }
    out[pos] = '\0';
}

bool NextionDriver::sendText_(const char* objectName, const char* value)
{
    if (!objectName || objectName[0] == '\0') return false;
    char safe[96]{};
    char cmd[TxBufSize]{};
    sanitizeText_(safe, sizeof(safe), value);

    const size_t nameLen = strlen(objectName);
    const size_t safeLen = strlen(safe);
    if (nameLen + safeLen + 8U >= sizeof(cmd)) return false;

    size_t pos = 0U;
    memcpy(cmd + pos, objectName, nameLen);
    pos += nameLen;
    cmd[pos++] = '.';
    cmd[pos++] = 't';
    cmd[pos++] = 'x';
    cmd[pos++] = 't';
    cmd[pos++] = '=';
    cmd[pos++] = '"';
    memcpy(cmd + pos, safe, safeLen);
    pos += safeLen;
    cmd[pos++] = '"';
    cmd[pos] = '\0';
    return sendCmd_(cmd);
}

const char* NextionDriver::homeTextObjectName_(HmiHomeTextField field) const
{
    switch (field) {
        case HmiHomeTextField::WaterTemp:
            return cfg_.waterTempTextObject;
        case HmiHomeTextField::AirTemp:
            return cfg_.airTempTextObject;
        case HmiHomeTextField::Ph:
            return cfg_.phTextObject;
        case HmiHomeTextField::Orp:
            return cfg_.orpTextObject;
        case HmiHomeTextField::Time:
            return cfg_.timeTextObject;
        case HmiHomeTextField::Date:
            return cfg_.dateTextObject;
        case HmiHomeTextField::ErrorMessage:
            return cfg_.errorMessageTextObject;
        default:
            return nullptr;
    }
}

const char* NextionDriver::homeGaugeObjectName_(HmiHomeGaugeField field) const
{
    switch (field) {
        case HmiHomeGaugeField::PhPercent:
            return cfg_.phGaugePercentObject;
        case HmiHomeGaugeField::OrpPercent:
            return cfg_.orpGaugePercentObject;
        default:
            return nullptr;
    }
}

bool NextionDriver::publishHomeText(HmiHomeTextField field, const char* text)
{
    if (!started_) return false;
    return sendText_(homeTextObjectName_(field), text);
}

bool NextionDriver::publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent)
{
    if (!started_) return false;
    return sendNum_(homeGaugeObjectName_(field), percent);
}

bool NextionDriver::publishHomeStateBits(uint32_t stateBits)
{
    if (!started_) return false;
    return sendNum_(cfg_.stateBitsObject, stateBits);
}

bool NextionDriver::publishHomeAlarmBits(uint32_t alarmBits)
{
    if (!started_) return false;
    return sendNum_(cfg_.alarmBitsObject, alarmBits);
}

bool NextionDriver::readNumberResponse_(uint32_t& value, uint16_t timeoutMs)
{
    value = 0U;
    if (!started_ || !cfg_.serial) return false;

    uint8_t frame[12]{};
    uint8_t len = 0U;
    uint8_t ffCount = 0U;
    const uint32_t start = millis();

    while ((uint32_t)(millis() - start) < (uint32_t)timeoutMs) {
        while (cfg_.serial->available() > 0) {
            const int rb = cfg_.serial->read();
            if (rb < 0) break;
            const uint8_t b = (uint8_t)rb;

            if (b == NEXTION_FF) {
                ++ffCount;
                if (ffCount >= 3U) {
                    if (len >= 5U && frame[0] == NEXTION_RSP_NUMBER) {
                        value = (uint32_t)frame[1] |
                                ((uint32_t)frame[2] << 8) |
                                ((uint32_t)frame[3] << 16) |
                                ((uint32_t)frame[4] << 24);
                        return true;
                    }
                    len = 0U;
                    ffCount = 0U;
                }
                continue;
            }

            while (ffCount > 0U) {
                if (len < sizeof(frame)) frame[len++] = NEXTION_FF;
                --ffCount;
            }

            if (len < sizeof(frame)) {
                frame[len++] = b;
            } else {
                len = 0U;
                ffCount = 0U;
            }
        }
        delay(1);
    }

    return false;
}

bool NextionDriver::readNumber_(const char* expr, uint32_t& value, uint16_t timeoutMs)
{
    if (!started_ || !cfg_.serial || !expr || expr[0] == '\0') return false;

    while (cfg_.serial->available() > 0) {
        (void)cfg_.serial->read();
    }
    customFrameActive_ = false;
    customExpectedLen_ = 0U;
    customLen_ = 0U;

    if (!sendCmdFmt_("get %s", expr)) return false;
    return readNumberResponse_(value, timeoutMs);
}

bool NextionDriver::readRtc(HmiRtcDateTime& out, uint16_t timeoutMs)
{
    out = HmiRtcDateTime{};
    if (!started_) return false;

    uint32_t v = 0U;
    if (!readNumber_("rtc0", v, timeoutMs)) return false;
    out.year = (uint16_t)v;
    if (!readNumber_("rtc1", v, timeoutMs)) return false;
    out.month = (uint8_t)v;
    if (!readNumber_("rtc2", v, timeoutMs)) return false;
    out.day = (uint8_t)v;
    if (!readNumber_("rtc3", v, timeoutMs)) return false;
    out.hour = (uint8_t)v;
    if (!readNumber_("rtc4", v, timeoutMs)) return false;
    out.minute = (uint8_t)v;
    if (!readNumber_("rtc5", v, timeoutMs)) return false;
    out.second = (uint8_t)v;
    return true;
}

bool NextionDriver::writeRtc(const HmiRtcDateTime& value)
{
    if (!started_) return false;

    bool ok = true;
    ok = sendCmdFmt_("rtc0=%u", (unsigned)value.year) && ok;
    ok = sendCmdFmt_("rtc1=%u", (unsigned)value.month) && ok;
    ok = sendCmdFmt_("rtc2=%u", (unsigned)value.day) && ok;
    ok = sendCmdFmt_("rtc3=%u", (unsigned)value.hour) && ok;
    ok = sendCmdFmt_("rtc4=%u", (unsigned)value.minute) && ok;
    ok = sendCmdFmt_("rtc5=%u", (unsigned)value.second) && ok;
    return ok;
}

bool NextionDriver::renderConfigMenu(const ConfigMenuView& view)
{
    if (!started_) return false;
    const uint32_t now = millis();
    if (cfg_.minRenderGapMs > 0 && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return true;
    }

    if (!pageReady_) return false;

    (void)sendText_("tPath", view.breadcrumb);
    (void)sendCmdFmt_("vis bHome,%u", view.canHome ? 1U : 0U);
    (void)sendCmdFmt_("vis bBack,%u", view.canBack ? 1U : 0U);
    (void)sendCmdFmt_("vis bValid,%u", view.canValidate ? 1U : 0U);
    (void)sendCmdFmt_("vis bPrev,%u", (view.pageIndex > 0U) ? 1U : 0U);
    (void)sendCmdFmt_("vis bNext,%u", (view.pageIndex + 1U < view.pageCount) ? 1U : 0U);
    (void)sendCmdFmt_("nPage.val=%u", (unsigned)(view.pageIndex + 1U));
    (void)sendCmdFmt_("nPages.val=%u", (unsigned)view.pageCount);

    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        char leftObj[8]{};
        char rightObj[8]{};
        char rowButtonObj[8]{};
        snprintf(leftObj, sizeof(leftObj), "tL%u", (unsigned)i);
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);
        snprintf(rowButtonObj, sizeof(rowButtonObj), "bR%u", (unsigned)i);

        const ConfigMenuRowView& row = view.rows[i];
        const bool showSwitch = row.visible &&
                                row.valueVisible &&
                                row.widget == ConfigMenuWidget::Switch;
        const bool showValue = row.visible && row.valueVisible;
        const bool showRowButton = row.visible &&
                                   view.mode == ConfigMenuMode::Browse &&
                                   row.canEdit;
        const bool leftTouchable = row.visible && view.mode == ConfigMenuMode::Browse;

        (void)sendCmdFmt_("vis %s,%u", leftObj, row.visible ? 1U : 0U);
        (void)sendCmdFmt_("tsw %s,%u", leftObj, leftTouchable ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", rightObj, showValue ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", rowButtonObj, showRowButton ? 1U : 0U);
        if (!showValue) {
            (void)sendCmdFmt_("tsw %s,0", rightObj);
            (void)sendCmdFmt_("%s.val=0", rightObj);
        }
        if (!row.visible) {
            continue;
        }

        char paddedLabel[64]{};
        snprintf(paddedLabel, sizeof(paddedLabel), " %s", row.label);
        (void)sendText_(leftObj, paddedLabel);
        if (!row.valueVisible) continue;

        if (showSwitch) {
            const bool on = (strcmp(row.value, "ON") == 0);
            (void)sendCmdFmt_("tsw %s,1", rightObj);
            (void)sendCmdFmt_("%s.val=%u", rightObj, on ? 1U : 0U);
            (void)sendText_(rightObj, on ? " ON" : " OFF");
            continue;
        }

        char displayVal[64]{};
        snprintf(displayVal, sizeof(displayVal), " %s%s", row.value, row.dirty ? " *" : "");
        (void)sendCmdFmt_("tsw %s,0", rightObj);
        (void)sendCmdFmt_("%s.val=0", rightObj);
        (void)sendText_(rightObj, displayVal);
    }

    lastRenderMs_ = now;
    return true;
}

bool NextionDriver::refreshConfigMenuValues(const ConfigMenuView& view)
{
    if (!started_ || !pageReady_) return false;

    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        if (!row.visible || !row.valueVisible) continue;

        char rightObj[8]{};
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);

        if (row.widget == ConfigMenuWidget::Switch) {
            const bool on = (strcmp(row.value, "ON") == 0);
            (void)sendCmdFmt_("tsw %s,1", rightObj);
            (void)sendCmdFmt_("%s.val=%u", rightObj, on ? 1U : 0U);
            (void)sendText_(rightObj, on ? " ON" : " OFF");
            continue;
        }

        char displayVal[64]{};
        snprintf(displayVal, sizeof(displayVal), " %s%s", row.value, row.dirty ? " *" : "");
        (void)sendCmdFmt_("tsw %s,0", rightObj);
        (void)sendCmdFmt_("%s.val=0", rightObj);
        (void)sendText_(rightObj, displayVal);
    }

    return true;
}

bool NextionDriver::parseCustomFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out)
{
    if (!frame || len == 0) return false;

    const uint8_t opcode = frame[0];
    const uint8_t* payload = frame + 1;
    const uint8_t payloadLen = (len > 0U) ? (uint8_t)(len - 1U) : 0U;

    switch (opcode) {
        case NEXTION_CMD_PAGE: {
            if (payloadLen < 1U) return false;
            const bool wasConfigPage = pageReady_;
            pageReady_ = (payload[0] == cfg_.configPageId);
            if (pageReady_) {
                out.type = HmiEventType::ConfigEnter;
                return true;
            }
            if (wasConfigPage) {
                out.type = HmiEventType::ConfigExit;
                return true;
            }
            return false;
        }

        case NEXTION_CMD_NAV:
            if (payloadLen < 1U) return false;
            switch (payload[0]) {
                case NAV_HOME:
                    out.type = HmiEventType::Home;
                    return true;
                case NAV_BACK:
                    out.type = HmiEventType::Back;
                    return true;
                case NAV_VALIDATE:
                    out.type = HmiEventType::Validate;
                    return true;
                case NAV_NEXT:
                    out.type = HmiEventType::NextPage;
                    return true;
                case NAV_PREV:
                    out.type = HmiEventType::PrevPage;
                    return true;
                case NAV_CONFIG_EXIT:
                    pageReady_ = false;
                    out.type = HmiEventType::ConfigExit;
                    return true;
                default:
                    return false;
            }

        case NEXTION_CMD_ROW_ACTIVATE:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowActivate;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_ROW_TOGGLE:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowToggle;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_ROW_CYCLE:
            if (payloadLen < 2U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowCycle;
            out.row = payload[0];
            out.direction = (payload[1] == 0U || payload[1] == 0xFFU) ? -1 : 1;
            return true;

        case NEXTION_CMD_ROW_EDIT:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowEdit;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_HOME_ACTION:
            if (payloadLen < 2U) return false;
            out.type = HmiEventType::Command;
            out.value = payload[1];
            switch (payload[0]) {
                case HOME_ACTION_FILTRATION:
                    out.command = HmiCommandId::HomeFiltrationSet;
                    return true;
                case HOME_ACTION_AUTO_MODE:
                    out.command = HmiCommandId::HomeAutoModeSet;
                    return true;
                case HOME_ACTION_SYNC:
                    out.command = HmiCommandId::HomeSyncRequest;
                    return true;
                case HOME_ACTION_CONFIG_OPEN:
                    out.command = HmiCommandId::HomeConfigOpen;
                    return true;
                case HOME_ACTION_PH_PUMP:
                    out.command = HmiCommandId::HomePhPumpSet;
                    return true;
                case HOME_ACTION_ORP_PUMP:
                    out.command = HmiCommandId::HomeOrpPumpSet;
                    return true;
                case HOME_ACTION_PH_PUMP_TOGGLE:
                    out.command = HmiCommandId::HomePhPumpToggle;
                    return true;
                case HOME_ACTION_ORP_PUMP_TOGGLE:
                    out.command = HmiCommandId::HomeOrpPumpToggle;
                    return true;
                case HOME_ACTION_FILTRATION_TOGGLE:
                    out.command = HmiCommandId::HomeFiltrationToggle;
                    return true;
                case HOME_ACTION_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeAutoModeToggle;
                    return true;
                case HOME_ACTION_ORP_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeOrpAutoModeToggle;
                    return true;
                case HOME_ACTION_PH_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomePhAutoModeToggle;
                    return true;
                case HOME_ACTION_WINTER_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeWinterModeToggle;
                    return true;
                case HOME_ACTION_LIGHTS_TOGGLE:
                    out.command = HmiCommandId::HomeLightsToggle;
                    return true;
                case HOME_ACTION_ROBOT_TOGGLE:
                    out.command = HmiCommandId::HomeRobotToggle;
                    return true;
                default:
                    return false;
            }

        default:
            return false;
    }
}

bool NextionDriver::pollEvent(HmiEvent& out)
{
    out = HmiEvent{};
    if (!started_ || !cfg_.serial) return false;

    while (cfg_.serial->available() > 0) {
        const int rb = cfg_.serial->read();
        if (rb < 0) break;
        const uint8_t b = (uint8_t)rb;

        if (customFrameActive_) {
            if (customExpectedLen_ == 0U) {
                if (b == 0U || b > CustomRxBufSize) {
                    customFrameActive_ = false;
                    customExpectedLen_ = 0U;
                    customLen_ = 0U;
                    continue;
                }
                customExpectedLen_ = b;
                customLen_ = 0U;
                continue;
            }

            customBuf_[customLen_++] = b;
            if (customLen_ >= customExpectedLen_) {
                const bool parsed = parseCustomFrame_(customBuf_, customExpectedLen_, out);
                customFrameActive_ = false;
                customExpectedLen_ = 0U;
                customLen_ = 0U;
                if (parsed) return true;
            }
            continue;
        }

        if (b == NEXTION_CUSTOM_START) {
            customFrameActive_ = true;
            customExpectedLen_ = 0U;
            customLen_ = 0U;
            continue;
        }
    }

    return false;
}
