#pragma once
#include <stddef.h>
#include <stdint.h>

// Command payloads (mode 't'), see PROTOCOL.md
const char OPEN_ON = '1', OPEN_HOLD = '2', OPEN_OFF = '3';
const char CLOSE_ON = '4', CLOSE_HOLD = '5', CLOSE_OFF = '6';
const char STOP = '3', KEEP_ALIVE = '7', GET_PARAMS = '8';

// Builds 0x02 | key | 't' | action | 0x03 | xor into `out` and returns its length.
// `out` must hold strlen(key) + 6 bytes.
inline size_t buildCommand(const char *key, char action, uint8_t *out) {
  size_t n = 0;
  out[n++] = 0x02;
  for (const char *k = key; *k; k++) out[n++] = *k;
  out[n++] = 't';
  out[n++] = action;
  out[n++] = 0x03;
  uint8_t x = 0xFF;
  for (size_t i = 0; i < n; i++) x ^= out[i];
  out[n++] = x;
  return n;
}
