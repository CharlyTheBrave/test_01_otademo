#pragma once

#include <stdint.h>

namespace OtaUpdate {

constexpr int httpTxBuffer = 4096;

bool begin();

// Call from one task once Wi-Fi and the system clock are ready. The public key
// is an uncompressed P-256 point (0x04, followed by 32 bytes each for X and Y).
// Returns true when the verified image is selected for the next boot.
bool checkAndInstall(const char *url, uint32_t firmwareVersion, const uint8_t (&publicKey)[65]);

} // namespace OtaUpdate
