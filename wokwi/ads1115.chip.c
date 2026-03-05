#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
  // pins
  pin_t scl, sda;
  pin_t addr0, addr1;
  pin_t ain[4];

  // i2c state
  uint8_t reg_ptr;     // pointer 0..3
  uint8_t rx_phase;    // 0=expect ptr, 1+=data
  uint8_t tx_index;    // 0/1
  uint8_t wbuf[2];
  uint8_t wcount;

  // registers
  uint16_t reg_conv;    // 0x00
  uint16_t reg_config;  // 0x01
  uint16_t reg_lo;      // 0x02
  uint16_t reg_hi;      // 0x03

  // conversion state
  bool converting;
  timer_t timer;

} chip_state_t;

// --- ADS1115 helpers ---

static float pga_full_scale_volts(uint16_t cfg) {
  switch ((cfg >> 9) & 0x7) {
    case 0: return 6.144f;
    case 1: return 4.096f;
    case 2: return 2.048f;
    case 3: return 1.024f;
    case 4: return 0.512f;
    case 5: return 0.256f;
    case 6: return 0.256f;
    case 7: return 0.256f;
  }
  return 4.096f;
}

static uint16_t dr_to_sps(uint16_t cfg) {
  // DR bits [7:5]
  switch ((cfg >> 5) & 0x7) {
    case 0: return 8;
    case 1: return 16;
    case 2: return 32;
    case 3: return 64;
    case 4: return 128;
    case 5: return 250;
    case 6: return 475;
    case 7: return 860;
  }
  return 128;
}

static bool mux_to_inputs(uint16_t cfg, int *pos, int *neg) {
  // MUX bits [14:12]
  uint8_t mux = (cfg >> 12) & 0x7;
  switch (mux) {
    case 0: *pos = 0; *neg = 1; return true; // A0-A1
    case 1: *pos = 0; *neg = 3; return true; // A0-A3
    case 2: *pos = 1; *neg = 3; return true; // A1-A3
    case 3: *pos = 2; *neg = 3; return true; // A2-A3
    case 4: *pos = 0; *neg = -1; return true; // A0-GND
    case 5: *pos = 1; *neg = -1; return true; // A1-GND
    case 6: *pos = 2; *neg = -1; return true; // A2-GND
    case 7: *pos = 3; *neg = -1; return true; // A3-GND
  }
  return false;
}

static uint16_t compute_conversion_code(chip_state_t *c) {
  int pos, neg;
  if (!mux_to_inputs(c->reg_config, &pos, &neg)) return 0;

  float vpos = pin_adc_read(c->ain[pos]);
  float vneg = (neg >= 0) ? pin_adc_read(c->ain[neg]) : 0.0f;

  float vdiff = vpos - vneg;   // differential can be negative
  float fs = pga_full_scale_volts(c->reg_config);

  // clamp to PGA range
  if (vdiff >  fs) vdiff =  fs;
  if (vdiff < -fs) vdiff = -fs;

  // map -FS..+FS to int16
  int32_t code = (int32_t)lroundf((vdiff / fs) * 32767.0f);
  if (code >  32767) code =  32767;
  if (code < -32768) code = -32768;

  return (uint16_t)((int16_t)code);
}

// --- conversion scheduling ---

static void finish_conversion(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;

  c->reg_conv = compute_conversion_code(c);
  c->converting = false;

  // set OS=1 (ready)
  c->reg_config |= 0x8000;

  // if continuous mode (MODE=0), schedule next one automatically
  bool continuous = ((c->reg_config >> 8) & 0x1) == 0;
  if (continuous) {
    c->converting = true;
    c->reg_config &= (uint16_t)~0x8000; // OS=0 while converting
    uint16_t sps = dr_to_sps(c->reg_config);
    uint32_t us = (uint32_t)(1000000UL / (uint32_t)sps);
    timer_start(c->timer, us, false);
  }
}

static void start_conversion(chip_state_t *c) {
  if (c->converting) return;

  c->converting = true;

  // OS=0 while converting (RobTillaart isReady() checks this)
  c->reg_config &= (uint16_t)~0x8000;

  uint16_t sps = dr_to_sps(c->reg_config);
  uint32_t us = (uint32_t)(1000000UL / (uint32_t)sps);
  if (us < 200) us = 200; // avoid too-small intervals
  timer_start(c->timer, us, false);
}

// --- I2C callbacks ---

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  (void)address; (void)read;
  chip_state_t *c = (chip_state_t *)user_data;
  c->tx_index = 0;
  c->rx_phase = 0;
  c->wcount = 0;
  return true;
}

static void commit_16bit_write(chip_state_t *c, uint16_t value) {
  switch (c->reg_ptr & 0x3) {
    case 0x01: { // CONFIG
      c->reg_config = value;

      // If OS bit written as 1 => trigger a conversion (single-shot / also works for continuous)
      if (c->reg_config & 0x8000) {
        start_conversion(c);
      }
      break;
    }
    case 0x02: c->reg_lo = value; break;
    case 0x03: c->reg_hi = value; break;
    default: /* writes to conversion ignored */ break;
  }
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *c = (chip_state_t *)user_data;

  if (c->rx_phase == 0) {
    c->reg_ptr = data & 0x3;
    c->rx_phase = 1;
    c->wcount = 0;
    return true;
  }

  // 16-bit MSB first
  c->wbuf[c->wcount++] = data;
  if (c->wcount == 2) {
    uint16_t v = ((uint16_t)c->wbuf[0] << 8) | c->wbuf[1];
    commit_16bit_write(c, v);
    c->wcount = 0;
  }
  return true;
}

static uint16_t get_reg_16(chip_state_t *c) {
  switch (c->reg_ptr & 0x3) {
    case 0x00: return c->reg_conv;
    case 0x01: return c->reg_config;
    case 0x02: return c->reg_lo;
    case 0x03: return c->reg_hi;
  }
  return 0;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;
  uint16_t v = get_reg_16(c);
  uint8_t out = (c->tx_index == 0) ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xFF);
  c->tx_index = (c->tx_index + 1) & 1;
  return out;
}

static void on_i2c_disconnect(void *user_data) {
  (void)user_data;
}

// --- address selection (ADDR1:ADDR0 -> 0x48..0x4B) ---

static uint8_t compute_addr(chip_state_t *c) {
  int a0 = pin_read(c->addr0) ? 1 : 0;
  int a1 = pin_read(c->addr1) ? 1 : 0;
  return (uint8_t)(0x48 + (a0 | (a1 << 1)));
}

void chip_init(void) {
  chip_state_t *c = (chip_state_t *)calloc(1, sizeof(chip_state_t));

  c->scl = pin_init("SCL", INPUT_PULLUP);
  c->sda = pin_init("SDA", INPUT_PULLUP);
  c->addr0 = pin_init("ADDR0", INPUT_PULLUP);
  c->addr1 = pin_init("ADDR1", INPUT_PULLUP);

  c->ain[0] = pin_init("AIN0", ANALOG);
  c->ain[1] = pin_init("AIN1", ANALOG);
  c->ain[2] = pin_init("AIN2", ANALOG);
  c->ain[3] = pin_init("AIN3", ANALOG);

  // Power-on defaults (close enough)
  c->reg_ptr = 0;
  c->reg_conv = 0;
  c->reg_config = 0x8583; // OS=1, PGA=+/-2.048 (ish), 128SPS, single-shot, etc.
  c->reg_lo = 0x8000;
  c->reg_hi = 0x7FFF;
  c->converting = false;

  // timer
  timer_config_t tcfg = {
    .callback = finish_conversion,
    .user_data = c,
  };
  c->timer = timer_init(&tcfg);

  // i2c
  static i2c_config_t i2c_cfg;
  i2c_cfg.address = compute_addr(c);
  i2c_cfg.scl = c->scl;
  i2c_cfg.sda = c->sda;
  i2c_cfg.connect = on_i2c_connect;
  i2c_cfg.read = on_i2c_read;
  i2c_cfg.write = on_i2c_write;
  i2c_cfg.disconnect = on_i2c_disconnect;
  i2c_cfg.user_data = c;

  i2c_init(&i2c_cfg);
}