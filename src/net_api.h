#pragma once
// Local HTTP API: lets anything on the home network (scripts, Home Assistant,
// shortcuts, other microcontrollers) read the bed and send commands. JSON over
// plain HTTP on port 8080, advertised over mDNS as bedremote.local
// (_http._tcp and _bedremote._tcp). There is no authentication: anyone on the
// same network can use it, so turn it off if you don't trust your network.
//
// It shares Wi-Fi with HomeKit (the radio is only on while HomeKit is on) and
// runs inside the HomeKit task, so it never competes with HomeSpan's sockets.
// Endpoints are documented in README.md ("Local API").

#include <stdint.h>

namespace NetApi {

constexpr uint16_t kPort = 8080;
constexpr const char* kHostName = "bedremote";  // <name>.local

void setEnabled(bool on);  // any task; takes effect on the next poll()
bool enabled();

// From the HomeKit task only:
void wifiUp(const char names[2][12]);  // once Wi-Fi first connects; names as published to HomeKit
void poll();                           // serves at most one request per call

}  // namespace NetApi
