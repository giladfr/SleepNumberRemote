#pragma once
// Fake HomeKit for the simulator: scripted Wi-Fi scan and pairing.
#include "homekit.h"

namespace SimHomeKit {
void pairPhone();      // the Home app adds the remote
void finishRestart();  // the remote came back after a restart
}
