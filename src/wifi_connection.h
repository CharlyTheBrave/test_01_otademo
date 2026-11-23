#pragma once

namespace WifiConnection {

// Read the NVS credentials and start the asynchronous station connection.
bool begin();
bool connected();

// Call from loop() to report connection changes and the initial timeout.
void reportStatus();

} // namespace WifiConnection
