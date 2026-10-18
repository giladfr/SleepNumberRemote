// In-memory settings for the simulator.
#include "settings.h"

#include <string.h>

#include "config.h"

namespace {
// Fixed example values (not config_local.h) so screenshots are the same on every machine.
RemoteSettings settings = {true, false, 255, 90, "12:34:56:78:9a:bc", 0, {"Alex", "Sam"}};
}

namespace Settings {
void load() {}
RemoteSettings& get() { return settings; }
void save() {}
}  // namespace Settings
