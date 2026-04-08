/**
 * @file St7789SupervisorDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Modules/SupervisorHMIModule/Drivers/FlowIoLogoBitmap.h"

namespace {
constexpr uint16_t rgb565_(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static constexpr uint16_t kColorBg = rgb565_(239, 245, 252);
static constexpr uint16_t kColorHeader = rgb565_(255, 255, 255);
static constexpr uint16_t kColorText = rgb565_(17, 24, 39);
static constexpr uint16_t kColorBrandFlow = rgb565_(7, 59, 102);
static constexpr uint16_t kColorBrandIo = rgb565_(0, 174, 239);
static constexpr uint16_t kColorMuted = rgb565_(108, 122, 141);
static constexpr uint16_t kColorDivider = rgb565_(217, 227, 239);
static constexpr uint16_t kColorWifiOff = rgb565_(200, 211, 226);
static constexpr uint16_t kColorOn = rgb565_(44, 204, 116);
static constexpr uint16_t kColorOff = rgb565_(148, 163, 184);
static constexpr uint16_t kColorAlarmAct = rgb565_(224, 72, 72);
static constexpr uint16_t kColorAlarmAck = rgb565_(240, 178, 85);
static constexpr uint16_t kColorGaugeOk = rgb565_(47, 158, 104);
static constexpr uint16_t kColorGaugeCardBg = rgb565_(255, 255, 255);
static constexpr uint16_t kColorGaugeTrack = rgb565_(222, 231, 242);
static constexpr uint16_t kColorBadgeDark = rgb565_(11, 18, 32);
static constexpr uint16_t kColorBadgeDarkOff = rgb565_(71, 85, 105);
static constexpr uint16_t kColorStatusGreen = rgb565_(48, 163, 104);
static constexpr uint16_t kColorStatusOrange = rgb565_(246, 186, 74);
static constexpr uint16_t kColorStatusRed = rgb565_(213, 75, 111);
static constexpr uint16_t kColorCheckFill = rgb565_(176, 239, 143);
static constexpr uint16_t kColorCheckMark = rgb565_(0, 144, 69);
static constexpr uint16_t kColorSystemFill = rgb565_(236, 252, 240);
static constexpr uint16_t kColorSystemBorder = rgb565_(197, 238, 208);
static constexpr uint16_t kColorSystemText = rgb565_(36, 156, 88);
static constexpr uint16_t kColorCardBorder = rgb565_(217, 227, 239);
static constexpr uint16_t kColorValue = rgb565_(14, 23, 43);
static constexpr uint16_t kColorWater = rgb565_(67, 131, 238);
static constexpr uint16_t kColorAir = rgb565_(27, 184, 219);
static constexpr uint16_t kColorPh = rgb565_(34, 197, 94);
static constexpr uint16_t kColorOrp = rgb565_(132, 82, 236);
static constexpr uint16_t kColorPsi = rgb565_(234, 88, 12);
static constexpr uint16_t kColorCounter = rgb565_(8, 145, 178);

static constexpr int16_t kHeaderH = 48;
static constexpr int16_t kSidePad = 8;
static constexpr int16_t kStatusPillW = 54;
static constexpr int16_t kStatusPillH = 20;
static constexpr int16_t kAlarmPillW = 54;
static constexpr int16_t kAlarmPillH = 20;
static constexpr int16_t kAlarmSummaryH = 28;
static constexpr uint8_t kMaxWifiBars = 5;
static constexpr uint8_t kRowCount = 7;

enum class SupervisorPage : uint8_t {
    Overview = 0,
    Metrics = 1,
};

struct AlarmRowDef {
    const char* label;
};

struct GaugeBand {
    float from;
    float to;
    uint16_t color;
};

static constexpr AlarmRowDef kAlarmRows[kSupervisorAlarmSlotCount] = {
    {"PSI Bas"},
    {"PSI Haut"},
    {"pH vide"},
    {"Chlore vide"},
    {"pH Uptime"},
    {"ORP Uptime"},
};

static constexpr GaugeBand kPhGaugeBands[] = {
    {6.4f, 6.8f, kColorAlarmAct},
    {6.8f, 7.0f, kColorAlarmAck},
    {7.0f, 7.6f, kColorGaugeOk},
    {7.6f, 7.8f, kColorAlarmAck},
    {7.8f, 8.4f, kColorAlarmAct},
};

static constexpr GaugeBand kOrpGaugeBands[] = {
    {350.0f, 500.0f, kColorAlarmAct},
    {500.0f, 620.0f, kColorAlarmAck},
    {620.0f, 760.0f, kColorGaugeOk},
    {760.0f, 820.0f, kColorAlarmAck},
    {820.0f, 900.0f, kColorAlarmAct},
};

static constexpr GaugeBand kWaterTempGaugeBands[] = {
    {0.0f, 8.0f, kColorAlarmAct},
    {8.0f, 14.0f, kColorAlarmAck},
    {14.0f, 30.0f, kColorGaugeOk},
    {30.0f, 34.0f, kColorAlarmAck},
    {34.0f, 40.0f, kColorAlarmAct},
};

static constexpr GaugeBand kAirTempGaugeBands[] = {
    {-10.0f, 0.0f, kColorAlarmAct},
    {0.0f, 8.0f, kColorAlarmAck},
    {8.0f, 28.0f, kColorGaugeOk},
    {28.0f, 35.0f, kColorAlarmAck},
    {35.0f, 45.0f, kColorAlarmAct},
};

uint16_t panelColor_(bool swapBytes, uint16_t c)
{
    if (!swapBytes) return c;
    return (uint16_t)((c << 8) | (c >> 8));
}

int clampi_(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float clampf_(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint8_t pctFromRssi_(int32_t dbm)
{
    const int v = clampi_((int)dbm, -100, -40);
    return (uint8_t)(((v + 100) * 100) / 60);
}

uint8_t barsFromPct_(uint8_t pct)
{
    return (uint8_t)((pct + 19U) / 20U);
}

int16_t textWidth_(const char* txt, uint8_t textSize)
{
    if (!txt) return 0;
    return (int16_t)(strlen(txt) * 6U * textSize);
}

void setDefaultFont_(SupervisorSt7789& d, bool swapBytes, uint16_t fg, uint16_t bg, uint8_t size)
{
    d.setFont(nullptr);
    d.setTextSize(size);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void setGfxFont_(SupervisorSt7789& d, bool swapBytes, const GFXfont* font, uint16_t fg, uint16_t bg)
{
    d.setFont(font);
    d.setTextSize(1);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void textBounds_(SupervisorSt7789& d,
                 const char* txt,
                 int16_t x,
                 int16_t y,
                 int16_t& x1,
                 int16_t& y1,
                 uint16_t& w,
                 uint16_t& h)
{
    if (!txt) txt = "";
    d.getTextBounds(txt, x, y, &x1, &y1, &w, &h);
}

int16_t gfxTextWidth_(SupervisorSt7789& d, const char* txt)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)w;
}

int16_t gfxBaselineCenteredInBox_(SupervisorSt7789& d, const char* txt, int16_t boxY, int16_t boxH)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)(boxY + ((boxH - (int16_t)h) / 2) - y1);
}

void drawGfxText_(SupervisorSt7789& d,
                  bool swapBytes,
                  const GFXfont* font,
                  uint16_t fg,
                  uint16_t bg,
                  int16_t x,
                  int16_t baselineY,
                  const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, baselineY);
    d.print(txt ? txt : "");
}

void drawGfxTextCenteredY_(SupervisorSt7789& d,
                           bool swapBytes,
                           const GFXfont* font,
                           uint16_t fg,
                           uint16_t bg,
                           int16_t x,
                           int16_t boxY,
                           int16_t boxH,
                           const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, gfxBaselineCenteredInBox_(d, txt, boxY, boxH));
    d.print(txt ? txt : "");
}

void drawDashedHLine_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, int16_t w, uint16_t color)
{
    for (int16_t dx = 0; dx < w; dx += 6) {
        d.drawFastHLine((int16_t)(x + dx), y, 4, panelColor_(swapBytes, color));
    }
}

void drawWifiBars_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint8_t bars)
{
    static constexpr uint16_t kWifiColors[kMaxWifiBars] = {
        rgb565_(92, 147, 255),
        rgb565_(78, 135, 252),
        rgb565_(62, 122, 247),
        rgb565_(40, 201, 171),
        rgb565_(30, 181, 136),
    };
    static constexpr int16_t kBarW = 7;
    static constexpr int16_t kStep = 10;
    static constexpr int16_t kUnitH = 5;

    for (uint8_t i = 1; i <= kMaxWifiBars; ++i) {
        const int16_t h = (int16_t)(i * kUnitH);
        const int16_t yi = (int16_t)(y + (kMaxWifiBars * kUnitH) - h);
        const uint16_t color = (i <= bars) ? kWifiColors[i - 1U] : kColorWifiOff;
        d.fillRoundRect((int16_t)(x + (int16_t)((i - 1) * kStep)),
                        yi,
                        kBarW,
                        h,
                        2,
                        panelColor_(swapBytes, color));
    }
}

void normalizeAlarmLabel_(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && (j + 1) < outLen; ++i) {
        const char c = in[i];
        out[j++] = (c == '_' || c == '-') ? ' ' : c;
    }
    out[j] = '\0';
}

void drawStatusPill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, bool on)
{
    const uint16_t fill = on ? kColorOn : kColorOff;
    const char* label = on ? "ON" : "OFF";

    d.fillRoundRect(x, y, kStatusPillW, kStatusPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kStatusPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kStatusPillH));
    d.print(label);
}

void drawBadge_(SupervisorSt7789& d,
                bool swapBytes,
                int16_t x,
                int16_t y,
                int16_t w,
                int16_t h,
                uint16_t fill,
                uint16_t stroke,
                uint16_t text,
                const char* label)
{
    d.fillRoundRect(x, y, w, h, (int16_t)(h / 2), panelColor_(swapBytes, fill));
    if (stroke != fill) {
        d.drawRoundRect(x, y, w, h, (int16_t)(h / 2), panelColor_(swapBytes, stroke));
    }
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, text, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((w - tw) / 2)),
                (int16_t)(gfxBaselineCenteredInBox_(d, label, y, h) + 1));
    d.print(label);
}

void drawBrandWordmark_(SupervisorSt7789& d,
                        bool swapBytes,
                        int16_t x,
                        int16_t y,
                        int16_t boxW,
                        int16_t boxH,
                        const GFXfont* font,
                        uint16_t bg);

void drawBootLogo_(SupervisorSt7789& d, bool swapBytes)
{
    const int16_t w = d.width();
    const int16_t h = d.height();
    d.fillScreen(panelColor_(swapBytes, rgb565_(255, 255, 255)));
    int16_t x = (int16_t)((w - (int16_t)kFlowIoLogoWidth) / 2);
    int16_t y = (int16_t)(((h - (int16_t)kFlowIoLogoHeight) / 2) - 30);
    if (x < 0) x = 0;
    if (y < 4) y = 4;

    d.drawRGBBitmap(x, y, kFlowIoLogoBitmap, kFlowIoLogoWidth, kFlowIoLogoHeight);

    int16_t textY = (int16_t)(y + (int16_t)kFlowIoLogoHeight + 8);
    const int16_t textH = 44;
    if ((int16_t)(textY + textH) > h) {
        textY = (int16_t)(h - textH);
    }
    drawBrandWordmark_(d, swapBytes, 0, textY, w, textH, &FreeSans18pt7b, kColorHeader);
}

void drawBrandWordmark_(SupervisorSt7789& d,
                        bool swapBytes,
                        int16_t x,
                        int16_t y,
                        int16_t boxW,
                        int16_t boxH,
                        const GFXfont* font,
                        uint16_t bg)
{
    static constexpr char kBrandFlow[] = "Flow";
    static constexpr char kBrandIo[] = ".io";
    static constexpr char kBrandFull[] = "Flow.io";

    setGfxFont_(d, swapBytes, font, kColorBrandFlow, bg);
    const int16_t flowW = gfxTextWidth_(d, kBrandFlow);
    setGfxFont_(d, swapBytes, font, kColorBrandIo, bg);
    const int16_t ioW = gfxTextWidth_(d, kBrandIo);
    const int16_t totalW = (int16_t)(flowW + ioW);
    const int16_t baseY = gfxBaselineCenteredInBox_(d, kBrandFull, y, boxH);
    const int16_t startX = (int16_t)(x + ((boxW - totalW) / 2));

    setGfxFont_(d, swapBytes, font, kColorBrandFlow, bg);
    d.setCursor(startX, baseY);
    d.print(kBrandFlow);

    setGfxFont_(d, swapBytes, font, kColorBrandIo, bg);
    d.setCursor((int16_t)(startX + flowW), baseY);
    d.print(kBrandIo);
}

void drawStaticLayout_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, SupervisorPage page)
{
    (void)h;
    d.fillScreen(panelColor_(swapBytes, kColorBg));
    if (page == SupervisorPage::Overview) {
        d.fillRect(0, 0, w, kHeaderH, panelColor_(swapBytes, kColorHeader));
    }
}

void drawHeaderWifi_(SupervisorSt7789& d, bool swapBytes, const SupervisorHmiViewModel& vm)
{
    d.fillRect(0, 0, 62, kHeaderH, panelColor_(swapBytes, kColorHeader));
    if (!vm.wifiConnected && vm.accessMode == NetworkAccessMode::AccessPoint) {
        drawGfxTextCenteredY_(d, swapBytes, &FreeSansBold12pt7b, kColorOn, kColorHeader, 9, 0, kHeaderH, "AP");
        return;
    }

    const uint8_t wifiBars = vm.hasRssi ? barsFromPct_(pctFromRssi_(vm.rssiDbm)) : 0U;
    drawWifiBars_(d, swapBytes, 8, 12, wifiBars);
}

void drawHeaderLogo_(SupervisorSt7789& d, bool swapBytes, int16_t w)
{
    const int16_t logoW = 116;
    const int16_t logoH = 26;
    const int16_t logoX = (int16_t)((w - logoW) / 2);
    const int16_t logoY = (int16_t)((kHeaderH - logoH) / 2);
    d.fillRect((int16_t)(logoX - 4), 0, (int16_t)(logoW + 8), kHeaderH, panelColor_(swapBytes, kColorHeader));
    drawBrandWordmark_(d, swapBytes, logoX, logoY, logoW, logoH, &FreeSansBold12pt7b, kColorHeader);
}

void drawHeaderTime_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* timeTxt)
{
    static constexpr int16_t kTimeAreaW = 98;
    d.fillRect((int16_t)(w - kTimeAreaW), 0, kTimeAreaW, kHeaderH, panelColor_(swapBytes, kColorHeader));
    setGfxFont_(d, swapBytes, &FreeSansBold12pt7b, kColorText, kColorHeader);
    const int16_t tw = gfxTextWidth_(d, timeTxt ? timeTxt : "--:--");
    d.setCursor((int16_t)(w - tw - 12), 31);
    d.print(timeTxt ? timeTxt : "--:--");
}

uint8_t systemState_(const SupervisorHmiViewModel& vm)
{
    if (!vm.flowLinkOk) return 0U;
    if (!vm.flowMqttReady) return 1U;
    return 2U;
}

uint16_t systemStateColor_(uint8_t state)
{
    if (state >= 2U) return kColorStatusGreen;
    if (state == 1U) return kColorStatusOrange;
    return kColorStatusRed;
}

void drawSystemStatus_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       uint8_t state)
{
    d.fillRect(x, y, w, 24, panelColor_(swapBytes, kColorGaugeCardBg));
    if (state >= 2U) {
        const int16_t cx = (int16_t)(x + (w / 2));
        const int16_t cy = (int16_t)(y + 12);
        d.fillCircle(cx, cy, 10, panelColor_(swapBytes, kColorCheckFill));
        for (int8_t off = -1; off <= 1; ++off) {
            d.drawLine((int16_t)(cx - 5), (int16_t)(cy + off), (int16_t)(cx - 1), (int16_t)(cy + 4 + off), panelColor_(swapBytes, kColorCheckMark));
            d.drawLine((int16_t)(cx - 1), (int16_t)(cy + 4 + off), (int16_t)(cx + 6), (int16_t)(cy - 4 + off), panelColor_(swapBytes, kColorCheckMark));
        }
        return;
    }

    const char* label = (state == 1U) ? "MQTT off" : "Flow indispo";
    setGfxFont_(d, swapBytes, &FreeSans9pt7b, systemStateColor_(state), kColorGaugeCardBg);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((w - tw) / 2)), (int16_t)(gfxBaselineCenteredInBox_(d, label, y, 24) + 1));
    d.print(label);
}

void drawCardLabel_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint16_t accent, const char* label)
{
    d.fillRoundRect(x, y, 30, 9, 4, panelColor_(swapBytes, accent));
    drawGfxText_(d, swapBytes, &FreeSans9pt7b, kColorMuted, kColorGaugeCardBg, (int16_t)(x + 40), (int16_t)(y + 10), label);
}

void drawRow_(SupervisorSt7789& d, bool swapBytes, int16_t w, uint8_t rowIndex, const char* label, bool on)
{
    const int16_t bodyTop = kHeaderH + 4;
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / kRowCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kStatusPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kStatusPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawStatusPill_(d, swapBytes, pillX, pillY, on);
}

const char* alarmStateLabel_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return "ACT";
        case SupervisorAlarmState::Acked: return "ACK";
        case SupervisorAlarmState::Clear:
        default:
            return "CLR";
    }
}

uint16_t alarmStateColor_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return kColorAlarmAct;
        case SupervisorAlarmState::Acked: return kColorAlarmAck;
        case SupervisorAlarmState::Clear:
        default:
            return kColorOff;
    }
}

void drawAlarmStatePill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, SupervisorAlarmState state)
{
    const uint16_t fill = alarmStateColor_(state);
    const char* label = alarmStateLabel_(state);

    d.fillRoundRect(x, y, kAlarmPillW, kAlarmPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kAlarmPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kAlarmPillH));
    d.print(label);
}

void drawAlarmSummary_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t w,
                       uint8_t actCount,
                       uint8_t ackCount,
                       uint8_t clrCount)
{
    const int16_t bodyTop = kHeaderH + 4;
    char summary[48] = {0};
    snprintf(summary, sizeof(summary), "Alarmes: %u ACT   %u ACK   %u CLR",
             (unsigned)actCount,
             (unsigned)ackCount,
             (unsigned)clrCount);

    d.fillRect(0, bodyTop, w, (int16_t)(kAlarmSummaryH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSansBold9pt7b, kColorText, kColorBg, kSidePad, bodyTop, kAlarmSummaryH, summary);
}

void drawAlarmRow_(SupervisorSt7789& d,
                   bool swapBytes,
                   int16_t w,
                   uint8_t rowIndex,
                   const char* label,
                   SupervisorAlarmState state)
{
    const int16_t bodyTop = (int16_t)(kHeaderH + 4 + kAlarmSummaryH);
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / (int16_t)kSupervisorAlarmSlotCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kAlarmPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kAlarmPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawAlarmStatePill_(d, swapBytes, pillX, pillY, state);
}

void drawAlarmBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, const SupervisorHmiViewModel& vm)
{
    drawAlarmSummary_(d, swapBytes, w, vm.flowAlarmActCount, vm.flowAlarmAckCount, vm.flowAlarmClrCount);
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        drawAlarmRow_(d, swapBytes, w, i, kAlarmRows[i].label, vm.flowAlarmStates[i]);
    }
}

float gaugeValueToAngle_(float value, float minValue, float maxValue)
{
    if (!(maxValue > minValue)) return -100.0f;
    const float ratio = (clampf_(value, minValue, maxValue) - minValue) / (maxValue - minValue);
    return -100.0f + (ratio * 200.0f);
}

void polarPoint_(int16_t cx, int16_t cy, float radius, float angleDeg, int16_t& x, int16_t& y)
{
    const float radians = angleDeg * 0.01745329252f;
    x = (int16_t)lroundf((float)cx + (cosf(radians) * radius));
    y = (int16_t)lroundf((float)cy + (sinf(radians) * radius));
}

void drawArcStroke_(SupervisorSt7789& d,
                    bool swapBytes,
                    int16_t cx,
                    int16_t cy,
                    int16_t radius,
                    float startDeg,
                    float endDeg,
                    uint16_t color,
                    int16_t thickness)
{
    if (!(endDeg > startDeg)) return;
    const int16_t half = (int16_t)(thickness / 2);
    for (int16_t offset = -half; offset <= half; ++offset) {
        const int16_t rr = (int16_t)(radius + offset);
        int16_t prevX = 0;
        int16_t prevY = 0;
        bool havePrev = false;
        for (float angle = startDeg; angle <= endDeg; angle += 4.0f) {
            int16_t x = 0;
            int16_t y = 0;
            polarPoint_(cx, cy, (float)rr, angle, x, y);
            if (havePrev) {
                d.drawLine(prevX, prevY, x, y, panelColor_(swapBytes, color));
            }
            prevX = x;
            prevY = y;
            havePrev = true;
        }
        int16_t endX = 0;
        int16_t endY = 0;
        polarPoint_(cx, cy, (float)rr, endDeg, endX, endY);
        d.drawLine(prevX, prevY, endX, endY, panelColor_(swapBytes, color));
    }
}

uint16_t resolveGaugeColor_(float value, bool hasValue, const GaugeBand* bands, size_t bandCount)
{
    if (!hasValue || !bands || bandCount == 0U) return kColorMuted;
    const float clamped = clampf_(value, bands[0].from, bands[bandCount - 1U].to);
    for (size_t i = 0; i < bandCount; ++i) {
        if (clamped >= bands[i].from && clamped <= bands[i].to) return bands[i].color;
    }
    return bands[bandCount - 1U].color;
}

void drawGaugeMarker_(SupervisorSt7789& d,
                      bool swapBytes,
                      int16_t cx,
                      int16_t cy,
                      int16_t radius,
                      float angleDeg,
                      uint16_t color)
{
    int16_t tipX = 0;
    int16_t tipY = 0;
    int16_t baseLX = 0;
    int16_t baseLY = 0;
    int16_t baseRX = 0;
    int16_t baseRY = 0;
    polarPoint_(cx, cy, (float)(radius + 4), angleDeg, tipX, tipY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg - 5.5f, baseLX, baseLY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg + 5.5f, baseRX, baseRY);
    d.fillTriangle(tipX, tipY, baseLX, baseLY, baseRX, baseRY, panelColor_(swapBytes, color));
}

void formatGaugeValue_(char* out, size_t outLen, bool hasValue, float value, uint8_t decimals, const char* unit)
{
    if (!out || outLen == 0U) return;
    if (!hasValue) {
        snprintf(out, outLen, "--");
        return;
    }
    char numberBuf[24] = {0};
    if (decimals > 0U) {
        snprintf(numberBuf, sizeof(numberBuf), "%.*f", (int)decimals, (double)value);
    } else {
        snprintf(numberBuf, sizeof(numberBuf), "%ld", lroundf(value));
    }
    if (unit && unit[0] != '\0') {
        snprintf(out, outLen, "%s %s", numberBuf, unit);
        return;
    }
    snprintf(out, outLen, "%s", numberBuf);
}

void drawMeasureGauge_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       const char* label,
                       bool hasValue,
                       float value,
                       float minValue,
                       float maxValue,
                       uint8_t decimals,
                       const char* unit,
                       const GaugeBand* bands,
                       size_t bandCount)
{
    d.fillRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorCardBorder));

    uint16_t accent = kColorWater;
    if (label && strcmp(label, "Air") == 0) accent = kColorAir;
    else if (label && strcmp(label, "pH") == 0) accent = kColorPh;
    else if (label && strcmp(label, "ORP") == 0) accent = kColorOrp;
    else if (label && strcmp(label, "Compteur") == 0) accent = kColorCounter;
    else if (label && strcmp(label, "BMP280") == 0) accent = kColorAir;
    else if (label && strcmp(label, "BME680") == 0) accent = kColorBrandIo;
    else if (label && strcmp(label, "PSI") == 0) accent = kColorPsi;
    drawCardLabel_(d, swapBytes, (int16_t)(x + 10), (int16_t)(y + 7), accent, label ? label : "");

    char valueBuf[24] = {0};
    formatGaugeValue_(valueBuf, sizeof(valueBuf), hasValue, value, decimals, nullptr);
    setGfxFont_(d, swapBytes, &FreeSansBold12pt7b, hasValue ? kColorValue : kColorMuted, kColorGaugeCardBg);
    const int16_t valueBaseY = (int16_t)(y + h - 11);
    d.setCursor((int16_t)(x + 12), valueBaseY);
    d.print(valueBuf);

    if (unit && unit[0] != '\0') {
        setGfxFont_(d, swapBytes, &FreeSans9pt7b, kColorMuted, kColorGaugeCardBg);
        if ((uint8_t)unit[0] == 0xB0 && unit[1] == 'C' && unit[2] == '\0') {
            const int16_t cw = gfxTextWidth_(d, "C");
            const int16_t unitX = (int16_t)(x + w - cw - 18);
            d.drawCircle((int16_t)(unitX - 6), (int16_t)(valueBaseY - 11), 2, panelColor_(swapBytes, kColorMuted));
            d.setCursor(unitX, valueBaseY);
            d.print("C");
        } else {
            const int16_t unitW = gfxTextWidth_(d, unit);
            const int16_t unitX = (int16_t)(x + w - unitW - 12);
            d.setCursor(unitX, valueBaseY);
            d.print(unit);
        }
    }

    (void)minValue;
    (void)maxValue;
    (void)bands;
    (void)bandCount;
}

uint16_t alarmTextColor_(const SupervisorHmiViewModel& vm, uint8_t alarmIndex)
{
    const uint32_t mask = (alarmIndex < 32U) ? (1UL << alarmIndex) : 0U;
    const bool active = (vm.flowAlarmActiveMask & mask) != 0U;
    const bool acked = (vm.flowAlarmAckedMask & mask) != 0U;
    const bool conditionTrue = (vm.flowAlarmConditionMask & mask) != 0U;

    if (active && !acked && conditionTrue) return kColorAlarmAct;
    if (active && !acked && !conditionTrue) return kColorAlarmAck;
    return kColorGaugeOk;
}

struct Rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

Rect overviewTopCardRect_(int16_t w, uint8_t column)
{
    const int16_t gap = 6;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    return Rect{
        (int16_t)(kSidePad + ((column > 0U) ? (cardW + gap) : 0)),
        (int16_t)(kHeaderH + 8),
        cardW,
        34
    };
}

Rect overviewAlarmCardRect_(int16_t w, int16_t h, uint8_t column)
{
    const Rect topCard = overviewTopCardRect_(w, 0U);
    const int16_t gap = 6;
    const int16_t top = (int16_t)(topCard.y + topCard.h + 8);
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    return Rect{
        (int16_t)(kSidePad + ((column > 0U) ? (cardW + gap) : 0)),
        top,
        cardW,
        (int16_t)(h - top - 8)
    };
}

Rect metricCardRect_(int16_t w, int16_t h, uint8_t index)
{
    const int16_t gap = 6;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    const int16_t cardH = (int16_t)((h - (2 * kSidePad) - (3 * gap)) / 4);
    const int16_t row = (int16_t)(index / 2U);
    const int16_t col = (int16_t)(index % 2U);
    return Rect{
        (int16_t)(kSidePad + (col * (cardW + gap))),
        (int16_t)(kSidePad + (row * (cardH + gap))),
        cardW,
        cardH
    };
}

bool sameRoundedValue_(bool hasA, float a, bool hasB, float b, uint8_t decimals)
{
    if (hasA != hasB) return false;
    if (!hasA) return true;

    float scale = 1.0f;
    for (uint8_t i = 0; i < decimals; ++i) scale *= 10.0f;
    return lroundf(a * scale) == lroundf(b * scale);
}

uint8_t wifiBarsForVm_(const SupervisorHmiViewModel& vm)
{
    return vm.hasRssi ? barsFromPct_(pctFromRssi_(vm.rssiDbm)) : 0U;
}

bool wifiHeaderChanged_(const SupervisorHmiViewModel& prev, const SupervisorHmiViewModel& curr)
{
    const bool prevAp = !prev.wifiConnected && prev.accessMode == NetworkAccessMode::AccessPoint;
    const bool currAp = !curr.wifiConnected && curr.accessMode == NetworkAccessMode::AccessPoint;
    if (prevAp != currAp) return true;
    if (currAp) return false;
    return wifiBarsForVm_(prev) != wifiBarsForVm_(curr);
}

bool ipTextChanged_(const SupervisorHmiViewModel& prev, const SupervisorHmiViewModel& curr)
{
    return strncmp(prev.ip, curr.ip, sizeof(curr.ip)) != 0;
}

bool alarmCardChanged_(const SupervisorHmiViewModel& prev, const SupervisorHmiViewModel& curr, uint8_t startIndex)
{
    for (uint8_t i = 0; i < 3U; ++i) {
        const uint8_t alarmIndex = (uint8_t)(startIndex + i);
        if (alarmIndex >= kSupervisorAlarmSlotCount) break;
        if (alarmTextColor_(prev, alarmIndex) != alarmTextColor_(curr, alarmIndex)) return true;
    }
    return false;
}

bool metricCardChanged_(const SupervisorHmiViewModel& prev, const SupervisorHmiViewModel& curr, uint8_t index)
{
    switch (index) {
        case 0: return !sameRoundedValue_(prev.flowHasWaterTemp, prev.flowWaterTemp, curr.flowHasWaterTemp, curr.flowWaterTemp, 1);
        case 1: return !sameRoundedValue_(prev.flowHasAirTemp, prev.flowAirTemp, curr.flowHasAirTemp, curr.flowAirTemp, 1);
        case 2: return !sameRoundedValue_(prev.flowHasPh, prev.flowPhValue, curr.flowHasPh, curr.flowPhValue, 2);
        case 3: return !sameRoundedValue_(prev.flowHasOrp, prev.flowOrpValue, curr.flowHasOrp, curr.flowOrpValue, 0);
        case 4: return !sameRoundedValue_(prev.flowHasWaterCounter, prev.flowWaterCounter, curr.flowHasWaterCounter, curr.flowWaterCounter, 1);
        case 5: return !sameRoundedValue_(prev.flowHasBme680Temp, prev.flowBme680Temp, curr.flowHasBme680Temp, curr.flowBme680Temp, 1);
        case 6: return !sameRoundedValue_(prev.flowHasBmp280Temp, prev.flowBmp280Temp, curr.flowHasBmp280Temp, curr.flowBmp280Temp, 1);
        case 7: return !sameRoundedValue_(prev.flowHasPsi, prev.flowPsi, curr.flowHasPsi, curr.flowPsi, 2);
        default: return true;
    }
}

void drawCenteredTextCard_(SupervisorSt7789& d,
                           bool swapBytes,
                           int16_t x,
                           int16_t y,
                           int16_t w,
                           int16_t h,
                           const char* text)
{
    d.fillRoundRect(x, y, w, h, 12, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(x, y, w, h, 12, panelColor_(swapBytes, kColorCardBorder));

    const char* value = (text && text[0] != '\0') ? text : "--";
    setGfxFont_(d, swapBytes, &FreeSans9pt7b, kColorValue, kColorGaugeCardBg);
    int16_t tw = gfxTextWidth_(d, value);
    if (tw > (int16_t)(w - 12)) {
        setDefaultFont_(d, swapBytes, kColorValue, kColorGaugeCardBg, 1);
        tw = textWidth_(value, 1);
        d.setCursor((int16_t)(x + ((w - tw) / 2)),
                    (int16_t)(y + ((h - 8) / 2) + 3));
        d.print(value);
        return;
    }

    const int16_t textX = (int16_t)(x + ((w - tw) / 2));
    const int16_t baseY = (int16_t)(gfxBaselineCenteredInBox_(d, value, (int16_t)(y - 2), (int16_t)(h - 8)) + 5);
    d.setCursor(textX, baseY);
    d.print(value);
}

void drawAlarmCard_(SupervisorSt7789& d,
                    bool swapBytes,
                    int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    const SupervisorHmiViewModel& vm,
                    uint8_t startIndex)
{
    d.fillRoundRect(x, y, w, h, 12, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(x, y, w, h, 12, panelColor_(swapBytes, kColorCardBorder));

    const int16_t innerY = (int16_t)(y + 6);
    const int16_t rowH = (int16_t)((h - 12) / 3);
    for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t alarmIndex = (uint8_t)(startIndex + i);
        if (alarmIndex >= kSupervisorAlarmSlotCount) break;

        const int16_t rowY = (int16_t)(innerY + ((int16_t)i * rowH));
        if (i > 0) {
            d.drawFastHLine((int16_t)(x + 10),
                            (int16_t)(rowY - 4),
                            (int16_t)(w - 20),
                            panelColor_(swapBytes, kColorDivider));
        }

        const uint16_t color = alarmTextColor_(vm, alarmIndex);
        drawGfxTextCenteredY_(d,
                              swapBytes,
                              &FreeSans9pt7b,
                              color,
                              kColorGaugeCardBg,
                              (int16_t)(x + 12),
                              rowY,
                              rowH,
                              kAlarmRows[alarmIndex].label);
    }
}

void drawOverviewBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, const SupervisorHmiViewModel& vm)
{
    const Rect leftTopCard = overviewTopCardRect_(w, 0U);
    const Rect rightTopCard = overviewTopCardRect_(w, 1U);
    const Rect leftCard = overviewAlarmCardRect_(w, h, 0U);
    const Rect rightCard = overviewAlarmCardRect_(w, h, 1U);

    drawCenteredTextCard_(d, swapBytes, leftTopCard.x, leftTopCard.y, leftTopCard.w, leftTopCard.h, vm.ip);
    drawCenteredTextCard_(d, swapBytes, rightTopCard.x, rightTopCard.y, rightTopCard.w, rightTopCard.h, "http://flowio.local");
    drawAlarmCard_(d, swapBytes, leftCard.x, leftCard.y, leftCard.w, leftCard.h, vm, 0U);
    drawAlarmCard_(d, swapBytes, rightCard.x, rightCard.y, rightCard.w, rightCard.h, vm, 3U);
}

void drawMetricCardByIndex_(SupervisorSt7789& d,
                            bool swapBytes,
                            int16_t w,
                            int16_t h,
                            uint8_t index,
                            const SupervisorHmiViewModel& vm)
{
    const Rect r = metricCardRect_(w, h, index);
    switch (index) {
        case 0:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "Eau", vm.flowHasWaterTemp, vm.flowWaterTemp,
                              0.0f, 40.0f, 1, "\xB0""C", kWaterTempGaugeBands,
                              sizeof(kWaterTempGaugeBands) / sizeof(kWaterTempGaugeBands[0]));
            break;
        case 1:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "Air", vm.flowHasAirTemp, vm.flowAirTemp,
                              -10.0f, 45.0f, 1, "\xB0""C", kAirTempGaugeBands,
                              sizeof(kAirTempGaugeBands) / sizeof(kAirTempGaugeBands[0]));
            break;
        case 2:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "pH", vm.flowHasPh, vm.flowPhValue,
                              6.4f, 8.4f, 2, "", kPhGaugeBands,
                              sizeof(kPhGaugeBands) / sizeof(kPhGaugeBands[0]));
            break;
        case 3:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "ORP", vm.flowHasOrp, vm.flowOrpValue,
                              350.0f, 900.0f, 0, "mV", kOrpGaugeBands,
                              sizeof(kOrpGaugeBands) / sizeof(kOrpGaugeBands[0]));
            break;
        case 4:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "Compteur", vm.flowHasWaterCounter, vm.flowWaterCounter,
                              0.0f, 0.0f, 1, "L", nullptr, 0U);
            break;
        case 5:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "BME680", vm.flowHasBme680Temp, vm.flowBme680Temp,
                              0.0f, 0.0f, 1, "\xB0""C", nullptr, 0U);
            break;
        case 6:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "BMP280", vm.flowHasBmp280Temp, vm.flowBmp280Temp,
                              0.0f, 0.0f, 1, "\xB0""C", nullptr, 0U);
            break;
        case 7:
        default:
            drawMeasureGauge_(d, swapBytes, r.x, r.y, r.w, r.h, "PSI", vm.flowHasPsi, vm.flowPsi,
                              0.0f, 0.0f, 2, "PSI", nullptr, 0U);
            break;
    }
}

void drawMeasuresBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, const SupervisorHmiViewModel& vm)
{
    for (uint8_t i = 0; i < 8U; ++i) {
        drawMetricCardByIndex_(d, swapBytes, w, h, i, vm);
    }
}
}

St7789SupervisorDriver::St7789SupervisorDriver(const St7789SupervisorDriverConfig& cfg)
    : cfg_(cfg),
      display_(&spiBus_, cfg.csPin, cfg.dcPin, cfg.rstPin)
{
}

bool St7789SupervisorDriver::begin()
{
    if (started_) return true;
    const bool swapBytes = cfg_.swapColorBytes;
    spiBus_.begin(cfg_.sclkPin, cfg_.misoPin, cfg_.mosiPin, cfg_.csPin);
    display_.setSPISpeed(cfg_.spiHz);
    display_.init(cfg_.resX, cfg_.resY);
    display_.setColRowStart(cfg_.colStart, cfg_.rowStart);
    display_.setRotation(cfg_.rotation & 0x03U);
    display_.invertDisplay(cfg_.invertColors);
    display_.fillScreen(panelColor_(swapBytes, kColorBg));
    display_.setTextWrap(false);
    display_.setTextSize(1);

    if (cfg_.backlightPin >= 0) {
        pinMode(cfg_.backlightPin, OUTPUT);
        digitalWrite(cfg_.backlightPin, HIGH);
        backlightOn_ = true;
    }

    drawBootLogo_(display_, swapBytes);
    delay(450);

    started_ = true;
    layoutDrawn_ = false;
    haveLastVm_ = false;
    lastRenderMs_ = 0;
    lastTime_[0] = '\0';
    lastPage_ = 0xFFU;
    memset(&lastVm_, 0, sizeof(lastVm_));
    return true;
}

void St7789SupervisorDriver::setBacklight(bool on)
{
    if (cfg_.backlightPin < 0) return;
    if (backlightOn_ == on) return;
    digitalWrite(cfg_.backlightPin, on ? HIGH : LOW);
    backlightOn_ = on;
}

const char* St7789SupervisorDriver::wifiStateText_(WifiState st) const
{
    switch (st) {
        case WifiState::Disabled: return "disabled";
        case WifiState::Idle: return "idle";
        case WifiState::Connecting: return "connecting";
        case WifiState::Connected: return "connected";
        case WifiState::ErrorWait: return "retry_wait";
        default: return "?";
    }
}

const char* St7789SupervisorDriver::netModeText_(NetworkAccessMode mode) const
{
    switch (mode) {
        case NetworkAccessMode::Station: return "sta";
        case NetworkAccessMode::AccessPoint: return "ap";
        case NetworkAccessMode::None:
        default:
            return "none";
    }
}

bool St7789SupervisorDriver::render(const SupervisorHmiViewModel& vm, bool force)
{
    if (!started_) return false;
    const uint32_t now = millis();
    if (!force && cfg_.minRenderGapMs > 0U && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return true;
    }

    const int16_t w = display_.width();
    const int16_t h = display_.height();
    const bool swapBytes = cfg_.swapColorBytes;
    const SupervisorPage page = (vm.pageIndex & 0x01U) == 0U
        ? SupervisorPage::Overview
        : SupervisorPage::Metrics;

    char timeBuf[16] = "--:--";
    time_t t = time(nullptr);
    if (t > 1600000000) {
        struct tm tmv{};
        localtime_r(&t, &tmv);
        (void)strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmv);
    }

    const bool fullRedraw = force || !layoutDrawn_ || lastPage_ != (uint8_t)page;
    if (fullRedraw) {
        drawStaticLayout_(display_, swapBytes, w, h, page);
        if (page == SupervisorPage::Overview) {
            drawHeaderWifi_(display_, swapBytes, vm);
            drawHeaderLogo_(display_, swapBytes, w);
            drawHeaderTime_(display_, swapBytes, w, timeBuf);
            drawOverviewBody_(display_, swapBytes, w, h, vm);
        } else {
            drawMeasuresBody_(display_, swapBytes, w, h, vm);
        }
    } else if (page == SupervisorPage::Overview) {
        if (!haveLastVm_ || wifiHeaderChanged_(lastVm_, vm)) {
            drawHeaderWifi_(display_, swapBytes, vm);
        }
        if (strncmp(lastTime_, timeBuf, sizeof(lastTime_)) != 0) {
            drawHeaderTime_(display_, swapBytes, w, timeBuf);
        }
        if (!haveLastVm_ || ipTextChanged_(lastVm_, vm)) {
            const Rect leftTopCard = overviewTopCardRect_(w, 0U);
            const Rect rightTopCard = overviewTopCardRect_(w, 1U);
            drawCenteredTextCard_(display_, swapBytes, leftTopCard.x, leftTopCard.y, leftTopCard.w, leftTopCard.h, vm.ip);
            drawCenteredTextCard_(display_, swapBytes, rightTopCard.x, rightTopCard.y, rightTopCard.w, rightTopCard.h, "http://flowio.local");
        }
        if (!haveLastVm_ || alarmCardChanged_(lastVm_, vm, 0U)) {
            const Rect leftCard = overviewAlarmCardRect_(w, h, 0U);
            drawAlarmCard_(display_, swapBytes, leftCard.x, leftCard.y, leftCard.w, leftCard.h, vm, 0U);
        }
        if (!haveLastVm_ || alarmCardChanged_(lastVm_, vm, 3U)) {
            const Rect rightCard = overviewAlarmCardRect_(w, h, 1U);
            drawAlarmCard_(display_, swapBytes, rightCard.x, rightCard.y, rightCard.w, rightCard.h, vm, 3U);
        }
    } else {
        for (uint8_t i = 0; i < 8U; ++i) {
            if (!haveLastVm_ || metricCardChanged_(lastVm_, vm, i)) {
                drawMetricCardByIndex_(display_, swapBytes, w, h, i, vm);
            }
        }
    }

    setDefaultFont_(display_, swapBytes, kColorText, kColorBg, 1);
    layoutDrawn_ = true;
    lastPage_ = (uint8_t)page;
    snprintf(lastTime_, sizeof(lastTime_), "%s", timeBuf);
    lastVm_ = vm;
    haveLastVm_ = true;
    lastRenderMs_ = now;
    return true;
}
