#pragma once

#include <WString.h>
#include <stdint.h>

namespace OtaUpdate {

constexpr int httpTxBuffer = 4096;

bool begin();

struct ReleaseDetails {
    uint32_t version = 0;
    String assetUrl;
    String tag;
    String title;
    String builtAt;
    String notes;
};

// Read only the published version file. Return a newer version, or 0 otherwise.
uint32_t quickCheck(const char *latestVersionUrl, uint32_t installedVersion);

// Read the asset header and fetch available metadata without writing to flash.
bool getReleaseDetails(const char *firmwareUrl, uint32_t installedVersion,
                       uint32_t publishedVersion, ReleaseDetails &details);

// Reopen the previewed asset, validate its version and signature, and select it
// for the next boot. Return the installed version on success, or 0 otherwise.
// The public key is an uncompressed P-256 point (0x04, then 32 bytes each for X and Y).
uint32_t installUpdate(const ReleaseDetails &details, uint32_t installedVersion,
                       const uint8_t (&publicKey)[65]);

} // namespace OtaUpdate
