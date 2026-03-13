/**
 * @file St7789SupervisorDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {
static constexpr uint16_t kColorBg = 0x0907;        // ~#0B1F3A
static constexpr uint16_t kColorTopBar = 0x198C;    // ~#1A3068
static constexpr uint16_t kColorMuted = 0x9492;
static constexpr uint16_t kColorSeparator = 0x5A4D;
static constexpr uint16_t kColorOffLed = 0x2945;

static constexpr int16_t kTopH = 34;
static constexpr int16_t kMid = 116;
static constexpr int16_t kLeftX = 8;
static constexpr int16_t kRightX = kMid + 8;
static constexpr int16_t kRowStep = 14;

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

uint8_t pctFromRssi_(int32_t dbm)
{
    const int v = clampi_((int)dbm, -100, -40);
    return (uint8_t)(((v + 100) * 100) / 60);
}

void drawWifiBars_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint8_t bars)
{
    static constexpr uint8_t kMaxBars = 6;
    static constexpr int16_t kBarW = 4;
    static constexpr int16_t kBarH = 3;

    for (uint8_t i = 1; i <= kMaxBars; i++) {
        const int16_t h = (int16_t)(i * kBarH);
        const int16_t yi = (int16_t)(y + (kMaxBars * kBarH) - h);
        const uint16_t c = (i <= bars) ? ST77XX_WHITE : kColorMuted;
        d.fillRect((int16_t)(x + (int16_t)((i - 1) * kBarW)), yi, kBarW - 1, h, panelColor_(swapBytes, c));
    }
}

void normalizeAlarmLabel_(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && (j + 1) < outLen; i++) {
        const char c = in[i];
        out[j++] = (c == '_' || c == '-') ? ' ' : c;
    }
    out[j] = '\0';
}

void drawStateRow_(SupervisorSt7789& d,
                   bool swapBytes,
                   int16_t x,
                   int16_t y,
                   const char* label,
                   bool on,
                   uint16_t onColor)
{
    const uint16_t ledColor = on ? onColor : kColorOffLed;
    d.fillRoundRect(x + 1, y + 1, 16, 8, 4, panelColor_(swapBytes, ledColor));
    d.drawRoundRect(x, y, 18, 10, 4, panelColor_(swapBytes, kColorSeparator));
    d.setTextColor(panelColor_(swapBytes, ST77XX_WHITE), panelColor_(swapBytes, kColorBg));
    d.setTextSize(1);
    d.setCursor((int16_t)(x + 22), y + 1);
    d.print(label ? label : "");
}

void drawStaticLayout_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h)
{
    d.fillScreen(panelColor_(swapBytes, kColorBg));
    d.setTextWrap(false);
    d.setTextSize(1);

    d.fillRect(0, 0, w, kTopH, panelColor_(swapBytes, kColorTopBar));
    d.drawFastVLine(kMid, kTopH + 4, (int16_t)(h - kTopH - 30), panelColor_(swapBytes, kColorSeparator));
    d.drawFastHLine(0, (int16_t)(h - 22), w, panelColor_(swapBytes, kColorTopBar));

    d.setTextColor(panelColor_(swapBytes, ST77XX_WHITE), panelColor_(swapBytes, kColorTopBar));
    d.setTextSize(2);
    d.setCursor(8, 9);
    d.print("Flow.IO");
}

void drawTextLine_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, const char* txt, uint16_t fg)
{
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, kColorBg));
    d.setTextSize(1);
    d.setCursor(x, y);
    d.print(txt ? txt : "");
}
}

St7789SupervisorDriver::St7789SupervisorDriver(const St7789SupervisorDriverConfig& cfg)
    : cfg_(cfg),
      display_(cfg.csPin, cfg.dcPin, cfg.rstPin)
{
}

bool St7789SupervisorDriver::begin()
{
    if (started_) return true;
    const bool swapBytes = cfg_.swapColorBytes;
    SPI.begin(cfg_.sclkPin, -1, cfg_.mosiPin, cfg_.csPin);
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

    display_.fillScreen(panelColor_(swapBytes, kColorBg));
    display_.setTextColor(panelColor_(swapBytes, ST77XX_WHITE), panelColor_(swapBytes, kColorBg));
    display_.setTextSize(2);
    display_.setCursor(14, 20);
    display_.print("Flow.IO");
    display_.setCursor(14, 48);
    display_.print("Supervisor");
    display_.setTextSize(1);
    display_.setCursor(14, 84);
    display_.print("HMI init...");
    delay(450);

    started_ = true;
    layoutDrawn_ = false;
    lastRenderMs_ = 0;
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
    const int16_t panelH = (int16_t)(h - kTopH - 30);
    const bool swapBytes = cfg_.swapColorBytes;
    char line[96] = {0};

    if (!layoutDrawn_ || force) {
        drawStaticLayout_(display_, swapBytes, w, h);
        layoutDrawn_ = true;
    }

    // Top-right WiFi icon only.
    const int32_t activeRssi = vm.flowHasRssi ? vm.flowRssiDbm : vm.rssiDbm;
    const bool hasAnyRssi = vm.flowHasRssi || vm.hasRssi;
    const uint8_t rssiPct = hasAnyRssi ? pctFromRssi_(activeRssi) : 0;
    const uint8_t wifiBars = (uint8_t)((rssiPct + 16U) / 17U);
    display_.fillRect((int16_t)(w - 30), 7, 28, 20, panelColor_(swapBytes, kColorTopBar));
    drawWifiBars_(display_, swapBytes, (int16_t)(w - 30), 8, wifiBars);

    // Dynamic panels.
    display_.fillRect(0, (int16_t)(kTopH + 4), (int16_t)(kMid - 2), panelH, panelColor_(swapBytes, kColorBg));
    display_.fillRect((int16_t)(kMid + 2), (int16_t)(kTopH + 4), (int16_t)(w - kMid - 2), panelH, panelColor_(swapBytes, kColorBg));

    // Left panel.
    int16_t ly = (int16_t)(kTopH + 10);
    char tbuf[16] = "--:--";
    char dbuf[16] = "--/--/--";
    time_t t = time(nullptr);
    if (t > 1600000000) {
        struct tm tmv{};
        localtime_r(&t, &tmv);
        (void)strftime(tbuf, sizeof(tbuf), "%H:%M", &tmv);
        (void)strftime(dbuf, sizeof(dbuf), "%d/%m/%y", &tmv);
    }

    snprintf(line, sizeof(line), "%s", dbuf);
    drawTextLine_(display_, swapBytes, kLeftX, ly, line, ST77XX_WHITE);
    ly += kRowStep;

    snprintf(line, sizeof(line), "%s", tbuf);
    drawTextLine_(display_, swapBytes, kLeftX, ly, line, kColorMuted);
    ly += kRowStep;

    snprintf(line, sizeof(line), "WiFi  %s", wifiStateText_(vm.wifiState));
    drawTextLine_(display_, swapBytes, kLeftX, ly, line, ST77XX_WHITE);
    ly += kRowStep;

    snprintf(line, sizeof(line), "Flow  %s", vm.flowLinkOk ? "linked" : "waiting");
    drawTextLine_(display_, swapBytes, kLeftX, ly, line, ST77XX_WHITE);
    ly += kRowStep;

    if (vm.ip[0]) {
        snprintf(line, sizeof(line), "IP    %s", vm.ip);
        drawTextLine_(display_, swapBytes, kLeftX, ly, line, ST77XX_WHITE);
    }

    // Right panel: Filtration auto, Hivernage, active alarms.
    int16_t ry = (int16_t)(kTopH + 10);
    const bool autoOn = vm.flowHasPoolModes && vm.flowFiltrationAuto;
    const bool winterOn = vm.flowHasPoolModes && vm.flowWinterMode;
    drawStateRow_(display_, swapBytes, kRightX, ry, "Filtration auto", autoOn, ST77XX_GREEN);
    ry += kRowStep;
    drawStateRow_(display_, swapBytes, kRightX, ry, "Hivernage", winterOn, ST77XX_CYAN);
    ry += kRowStep;

    const int16_t yLimit = (int16_t)(h - 30);
    uint8_t shownAlarms = 0;
    if (vm.flowAlarmActiveCount == 0) {
        drawStateRow_(display_, swapBytes, kRightX, ry, "Aucune alarme", true, ST77XX_GREEN);
    } else {
        for (uint8_t i = 0; i < vm.flowAlarmCodeCount; i++) {
            if ((int16_t)(ry + 10) >= yLimit) break;
            char alarmLabel[28] = {0};
            normalizeAlarmLabel_(vm.flowAlarmCodes[i], alarmLabel, sizeof(alarmLabel));
            if (alarmLabel[0] == '\0') continue;
            drawStateRow_(display_, swapBytes, kRightX, ry, alarmLabel, true, ST77XX_RED);
            ry += kRowStep;
            shownAlarms++;
        }

        const uint8_t remaining = (vm.flowAlarmActiveCount > shownAlarms)
                                      ? (uint8_t)(vm.flowAlarmActiveCount - shownAlarms)
                                      : 0U;
        if (remaining > 0U && (int16_t)(ry + 10) < yLimit) {
            char remainLabel[24] = {0};
            snprintf(remainLabel, sizeof(remainLabel), "+%u autres", (unsigned)remaining);
            drawStateRow_(display_, swapBytes, kRightX, ry, remainLabel, true, ST77XX_RED);
        }
    }

    // Bottom banner.
    display_.fillRect(4, (int16_t)(h - 14), (int16_t)(w - 8), 10, panelColor_(swapBytes, kColorBg));
    display_.setTextColor(panelColor_(swapBytes, ST77XX_WHITE), panelColor_(swapBytes, kColorBg));
    display_.setTextSize(1);
    display_.setCursor(4, (int16_t)(h - 14));
    snprintf(line, sizeof(line), "%s", vm.banner[0] ? vm.banner : "Hold WiFi button 3s to reset credentials");
    display_.print(line);

    lastRenderMs_ = now;
    return true;
}
