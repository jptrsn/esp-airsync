#ifndef I2S_OUTPUT_H
#define I2S_OUTPUT_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialize I2S output with the given pin assignments.
 *        Pin values should come from config_manager_load().
 *
 * @param bck_pin  Bit clock pin
 * @param ws_pin   Word select (LCK) pin
 * @param dout_pin Data out pin
 */
void i2s_output_init(int bck_pin, int ws_pin, int dout_pin);

// Write PCM audio data to I2S.
//
// No volume scaling happens here: software volume is applied exactly once,
// upstream in the RAOP audio buffer (see
// components/esp-raop-receiver/src/raop_volume.h), whose Q16 gain is the only
// PCM scaling stage in the output path. Data arriving here is already
// attenuated in the digital domain.
void i2s_output_write(const uint8_t *data, size_t len);

// Stop and release I2S channel
void i2s_output_deinit(void);

#endif // I2S_OUTPUT_H