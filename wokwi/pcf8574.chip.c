// Wokwi Custom Chip - For information and examples see:
// https://link.wokwi.com/custom-chips-alpha
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2022 Uri Shaked / wokwi.com

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

const int BASEADDRESS = 0x20;

typedef struct {
  pin_t PIN_SDA;
  pin_t PIN_SCL;
  pin_t PIN_INT;
  pin_t PIN_A0;
  pin_t PIN_A1;
  pin_t PIN_A2;
  pin_t PIN_P0;
  pin_t PIN_P1;
  pin_t PIN_P2;
  pin_t PIN_P3;
  pin_t PIN_P4;
  pin_t PIN_P5;
  pin_t PIN_P6;
  pin_t PIN_P7;
  uint8_t data;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t dataIn);
static void on_i2c_disconnect(void *user_data);

void chip_init() {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->PIN_INT = pin_init("INT", OUTPUT);
  chip->PIN_A0 = pin_init("A0", INPUT_PULLUP);
  chip->PIN_A1 = pin_init("A1", INPUT_PULLUP);
  chip->PIN_A2 = pin_init("A2", INPUT_PULLUP);
  chip->PIN_P0 = pin_init("P0", OUTPUT);
  chip->PIN_P1 = pin_init("P1", OUTPUT);
  chip->PIN_P2 = pin_init("P2", OUTPUT);
  chip->PIN_P3 = pin_init("P3", OUTPUT);
  chip->PIN_P4 = pin_init("P4", OUTPUT);
  chip->PIN_P5 = pin_init("P5", OUTPUT);
  chip->PIN_P6 = pin_init("P6", OUTPUT);
  chip->PIN_P7 = pin_init("P7", OUTPUT);
  chip->data = 0;

  uint8_t address = BASEADDRESS + pin_read(chip->PIN_A0) + 2 * pin_read(chip->PIN_A1) + 4 * pin_read(chip->PIN_A2);
  // printf("%x",address);

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = address,
    .scl = pin_init("SCL", INPUT),
    .sda = pin_init("SDA", INPUT),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect, // Optional
  };
  i2c_init(&i2c_config);
}

static void write_pins(chip_state_t *chip) {
  pin_write(chip->PIN_P0, chip->data & 0x01);
  pin_write(chip->PIN_P1, chip->data >> 1 & 0x01);
  pin_write(chip->PIN_P2, chip->data >> 2 & 0x01);
  pin_write(chip->PIN_P3, chip->data >> 3 & 0x01);
  pin_write(chip->PIN_P4, chip->data >> 4 & 0x01);
  pin_write(chip->PIN_P5, chip->data >> 5 & 0x01);
  pin_write(chip->PIN_P6, chip->data >> 6 & 0x01);
  pin_write(chip->PIN_P7, chip->data >> 7 & 0x01);
}

bool on_i2c_connect(void *user_data, uint32_t address, bool connect) {
  return true; /* Ack */
}

uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = user_data;
  uint8_t tmpData = 0;
  tmpData |= pin_read(chip->PIN_P0) ;
  tmpData |= pin_read(chip->PIN_P1) << 1;
  tmpData |= pin_read(chip->PIN_P2) << 2;
  tmpData |= pin_read(chip->PIN_P3) << 3;
  tmpData |= pin_read(chip->PIN_P4) << 4;
  tmpData |= pin_read(chip->PIN_P5) << 5;
  tmpData |= pin_read(chip->PIN_P6) << 6;
  tmpData |= pin_read(chip->PIN_P7) << 7;
  chip->data = tmpData;

  return tmpData;
}

bool on_i2c_write(void *user_data, uint8_t dataIn) {
  chip_state_t *chip = user_data;
  chip->data = dataIn;
  write_pins(chip);
  return true; // Ack
}

void on_i2c_disconnect(void *user_data) {
  // Do nothing
}
