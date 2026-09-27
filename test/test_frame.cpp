// Host-side check of the frame builder: g++ -std=c++17 -I src test/test_frame.cpp -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "frame.h"

int main() {
  uint8_t buf[16];
  // "open ON" with key 1234: 02 '1' '2' '3' '4' 't' '1' 03, xor = 0xFF ^ all = 0xBF
  const uint8_t expected[] = {0x02, '1', '2', '3', '4', 't', '1', 0x03, 0xBF};
  size_t n = buildCommand("1234", OPEN_ON, buf);
  assert(n == sizeof(expected));
  assert(memcmp(buf, expected, n) == 0);

  // Checksum property the app verifies: xor of everything including the checksum is 0xFF
  n = buildCommand("1234", CLOSE_HOLD, buf);
  uint8_t x = 0;
  for (size_t i = 0; i < n; i++) x ^= buf[i];
  assert(x == 0xFF);

  puts("frame ok");
}
