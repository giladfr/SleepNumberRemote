#pragma once
// Fake bed for the simulator: the state the UI sees plus scripted behaviour.
#include "bed_link.h"

namespace SimBed {
BedState& state();
void reset();  // connected bed with the values read from the real bed
}
