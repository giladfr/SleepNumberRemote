#pragma once
// Local Bluetooth link to a Sleep Number "Fuzion" bed (Climate / FlexFit bases).
//
// Commands are Fuzion "bamkey" text commands (e.g. "PSNS left 40") wrapped in a
// "fUzIoN" + length + CRC32 frame and written to the BamKey characteristic.
// After bonding, the Auth characteristic yields a 16-byte session id; the bed
// notifies that id when a response is ready to be read back.
// Protocol reference: github.com/kristofferR/ha-adjustable-bed (sleep_number.py).
//
// Sides in this API are on-screen sides; Settings.swapSides maps them to the
// bed's own "left"/"right".

#include <stdint.h>

enum BedSide : uint8_t { SIDE_LEFT = 0, SIDE_RIGHT = 1 };
enum Actuator : uint8_t { ACT_HEAD = 0, ACT_FOOT = 1 };

enum class LinkState : uint8_t { Asleep, NoBed, Scanning, Connecting, NeedsPairing, Busy, Ready, Error };

struct LogEntry {
  char cmd[28];
  char resp[40];
  uint16_t ms;
  bool ok;
};

struct FoundBed {
  char addr[18];
  char name[24];
  int8_t rssi;
  uint8_t addrType;
};

constexpr int kLogSize = 8;
constexpr int kMaxFound = 5;

struct BedState {
  LinkState link = LinkState::Asleep;
  char status[40] = "Starting";
  uint32_t version = 0;  // bumped on every change

  // Link diagnostics
  char bedAddr[18] = "";
  char bedName[24] = "";
  int8_t rssi = 0;
  uint16_t mtu = 0;
  uint8_t bonds = 0;
  uint32_t connectedAt = 0;  // millis() when connected, 0 = not connected
  uint32_t cmdOk = 0, cmdFail = 0;
  char lastError[44] = "";

  // Scan results for pairing
  bool scanning = false;
  uint8_t foundCount = 0;
  FoundBed found[kMaxFound] = {};

  // Bed configuration (SYCG / SYAG)
  bool configLoaded = false;
  char model[32] = "";
  char rawConfig[80] = "";
  bool hasPressure = true, hasArticulation = true, hasLight = false, hasFootwarming = false;
  char thermal[12] = "";
  bool hasActuator[2][2] = {{true, true}, {true, true}};  // [side][actuator]
  bool presetFlat = true, presetFavorite = true, presetSnore = true, presetZeroG = true;
  bool presetTv = false, presetRead = false;

  // Live values, -1 = unknown
  int sleepNumber[2] = {-1, -1};  // user setting
  int ambient[2] = {-1, -1};      // measured
  int favorite[2] = {-1, -1};
  bool adjusting[2] = {false, false};
  int position[2][2] = {{-1, -1}, {-1, -1}};  // [side][actuator], 0-100
  char preset[2][16] = {"", ""};
  bool lightOn = false;
  char lightRaw[16] = "";
  bool moving = false;

  // Recent commands (ring buffer; newest is log[(logHead + kLogSize - 1) % kLogSize])
  LogEntry log[kLogSize] = {};
  uint8_t logCount = 0, logHead = 0;
};

enum class CmdType : uint8_t { SetSleepNumber, SetFavorite, Preset, SetActuator, HaltAll, Light };

struct BedCmd {
  CmdType type;
  BedSide side = SIDE_LEFT;
  Actuator actuator = ACT_HEAD;
  int value = 0;
  char preset[12] = "";
};

namespace BedLink {
void begin();
// While inactive the link disconnects so phones can use the SleepIQ app.
void setActive(bool active);
bool send(const BedCmd& cmd);
BedState snapshot();

void startScan();                     // fills BedState.found
void pairWith(const FoundBed& bed);   // select + fresh pairing (bed must be in pairing mode)
void forgetPairing();                 // drop all bonds
void reconnect();                     // re-read settings (e.g. side swap) and reconnect
}  // namespace BedLink
