// Wokwi Custom Chip: TCA9554 / TCA9554PWR
// 8-bit I2C I/O expander
//
// Registers:
//   0x00 Input Port register       (read)
//   0x01 Output Port register      (read/write)
//   0x02 Polarity Inversion        (read/write)
//   0x03 Configuration register    (read/write, 1=input, 0=output)
//
// Default power-on state:
//   Output Port   = 0xFF
//   Polarity      = 0x00
//   Configuration = 0xFF  (all pins input)
//
// I2C address:
//   TCA9554 uses 0x20..0x27, selected by pins A0/A1/A2.
//   Leave A0/A1/A2 low for address 0x20.

#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TCA9554_BASE_ADDR 0x20

#define REG_INPUT        0x00
#define REG_OUTPUT       0x01
#define REG_POLARITY     0x02
#define REG_CONFIG       0x03

typedef struct {
  pin_t sda;
  pin_t scl;
  pin_t addr[3];
  pin_t port[8];
  pin_t intr;

  uint8_t selected_reg;
  uint8_t write_count;

  uint8_t reg_output;
  uint8_t reg_polarity;
  uint8_t reg_config;

  uint8_t last_input_raw;
} chip_state_t;

static uint8_t get_i2c_address(chip_state_t *chip);
static void apply_port_outputs(chip_state_t *chip);
static uint8_t read_port_raw(chip_state_t *chip);
static uint8_t read_register(chip_state_t *chip, uint8_t reg);
static void write_register(chip_state_t *chip, uint8_t reg, uint8_t value);
static void set_interrupt(chip_state_t *chip, bool active);
static void update_interrupt(chip_state_t *chip);

static bool on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);
static void on_port_pin_change(void *user_data, pin_t pin, uint32_t value);

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));

  chip->addr[0] = pin_init("A0", INPUT_PULLDOWN);
  chip->addr[1] = pin_init("A1", INPUT_PULLDOWN);
  chip->addr[2] = pin_init("A2", INPUT_PULLDOWN);

  chip->port[0] = pin_init("P0", INPUT);
  chip->port[1] = pin_init("P1", INPUT);
  chip->port[2] = pin_init("P2", INPUT);
  chip->port[3] = pin_init("P3", INPUT);
  chip->port[4] = pin_init("P4", INPUT);
  chip->port[5] = pin_init("P5", INPUT);
  chip->port[6] = pin_init("P6", INPUT);
  chip->port[7] = pin_init("P7", INPUT);

  // Real INT is open-drain active-low. In Wokwi, emulate idle state as input+pullup
  // and active state as a driven LOW output.
  chip->intr = pin_init("INT", INPUT_PULLUP);

  chip->selected_reg = REG_INPUT;
  chip->write_count = 0;
  chip->reg_output = 0xFF;
  chip->reg_polarity = 0x00;
  chip->reg_config = 0xFF;   // 1=input, 0=output

  apply_port_outputs(chip);
  chip->last_input_raw = read_port_raw(chip);
  set_interrupt(chip, false);

  const pin_watch_config_t watch_config = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_port_pin_change,
  };

  for (int i = 0; i < 8; i++) {
    pin_watch(chip->port[i], &watch_config);
  }

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = 0, // listen to all addresses, ACK only 0x20..0x27 according to A0/A1/A2
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };

  i2c_init(&i2c_config);

  printf("TCA9554 custom chip initialized at I2C base 0x20, current address 0x%02x\n",
         get_i2c_address(chip));
}

static uint8_t get_i2c_address(chip_state_t *chip) {
  uint8_t address = TCA9554_BASE_ADDR;

  if (pin_read(chip->addr[0]) == HIGH) {
    address |= 0x01;
  }
  if (pin_read(chip->addr[1]) == HIGH) {
    address |= 0x02;
  }
  if (pin_read(chip->addr[2]) == HIGH) {
    address |= 0x04;
  }

  return address;
}

static void apply_port_outputs(chip_state_t *chip) {
  for (int i = 0; i < 8; i++) {
    const uint8_t bit = (uint8_t)(1u << i);

    if (chip->reg_config & bit) {
      // TCA9554 configuration bit = 1 means input.
      pin_mode(chip->port[i], INPUT);
    } else {
      // TCA9554 configuration bit = 0 means output.
      pin_mode(chip->port[i], (chip->reg_output & bit) ? OUTPUT_HIGH : OUTPUT_LOW);
    }
  }

  update_interrupt(chip);
}

static uint8_t read_port_raw(chip_state_t *chip) {
  uint8_t value = 0;

  for (int i = 0; i < 8; i++) {
    const uint8_t bit = (uint8_t)(1u << i);

    if (chip->reg_config & bit) {
      // Input pin: read the external state.
      if (pin_read(chip->port[i]) == HIGH) {
        value |= bit;
      }
    } else {
      // Output pin: report the latched output value.
      if (chip->reg_output & bit) {
        value |= bit;
      }
    }
  }

  return value;
}

static uint8_t read_register(chip_state_t *chip, uint8_t reg) {
  switch (reg) {
    case REG_INPUT: {
      const uint8_t raw = read_port_raw(chip);
      chip->last_input_raw = raw;
      set_interrupt(chip, false);
      return raw ^ chip->reg_polarity;
    }

    case REG_OUTPUT:
      return chip->reg_output;

    case REG_POLARITY:
      return chip->reg_polarity;

    case REG_CONFIG:
      return chip->reg_config;

    default:
      return 0xFF;
  }
}

static void write_register(chip_state_t *chip, uint8_t reg, uint8_t value) {
  switch (reg) {
    case REG_INPUT:
      // Input register is read-only.
      break;

    case REG_OUTPUT:
      chip->reg_output = value;
      apply_port_outputs(chip);
      break;

    case REG_POLARITY:
      chip->reg_polarity = value;
      update_interrupt(chip);
      break;

    case REG_CONFIG:
      chip->reg_config = value;
      apply_port_outputs(chip);
      break;
  }
}

static void set_interrupt(chip_state_t *chip, bool active) {
  if (active) {
    pin_mode(chip->intr, OUTPUT_LOW);
  } else {
    pin_mode(chip->intr, INPUT_PULLUP);
  }
}

static void update_interrupt(chip_state_t *chip) {
  // Interrupt only matters for pins configured as inputs.
  const uint8_t input_mask = chip->reg_config;
  const uint8_t raw = read_port_raw(chip);

  if (((raw ^ chip->last_input_raw) & input_mask) != 0) {
    set_interrupt(chip, true);
  } else {
    set_interrupt(chip, false);
  }
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (address != get_i2c_address(chip)) {
    return false; // NACK unrelated I2C addresses
  }

  if (!read) {
    chip->write_count = 0;
  }

  return true; // ACK
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  return read_register(chip, chip->selected_reg);
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->write_count == 0) {
    // First byte of a write transaction is the command/register pointer.
    if (data > REG_CONFIG) {
      return false; // invalid command/register
    }

    chip->selected_reg = data;
    chip->write_count++;
    return true;
  }

  // Subsequent bytes write to the selected register.
  write_register(chip, chip->selected_reg, data);
  chip->write_count++;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->write_count = 0;
}

static void on_port_pin_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;

  chip_state_t *chip = (chip_state_t *)user_data;
  update_interrupt(chip);
}
