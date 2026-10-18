// Fake BedLink: reacts to commands like the real bed would (instantly or after
// a short simulated delay) so the UI can be exercised without hardware.
#include <Arduino.h>

#include "bed_link.h"
#include "sim_bed.h"

namespace {
BedState bed;
uint32_t moveUntil = 0, scanUntil = 0, pairReadyAt = 0, adjustUntil[2] = {0, 0};

void log(const char* cmd, const char* resp, bool ok = true) {
  LogEntry& e = bed.log[bed.logHead];
  strlcpy(e.cmd, cmd, sizeof(e.cmd));
  strlcpy(e.resp, resp, sizeof(e.resp));
  e.ms = 60 + (millis() % 50);
  e.ok = ok;
  bed.logHead = (bed.logHead + 1) % kLogSize;
  if (bed.logCount < kLogSize) bed.logCount++;
  ok ? bed.cmdOk++ : bed.cmdFail++;
}

// On-screen side -> wire side (the sim runs with swapped sides)
const char* wire(int side) { return side == SIDE_LEFT ? "right" : "left"; }
}  // namespace

namespace SimBed {

BedState& state() { return bed; }

void reset() {
  bed = BedState();
  bed.link = LinkState::Ready;
  strlcpy(bed.status, "Connected", sizeof(bed.status));
  strlcpy(bed.bedAddr, "12:34:56:78:9a:bc", sizeof(bed.bedAddr));
  strlcpy(bed.bedName, "Smart bed 789ABC", sizeof(bed.bedName));
  bed.rssi = -67;
  bed.mtu = 255;
  bed.bonds = 1;
  bed.connectedAt = 1;
  bed.configLoaded = true;
  strlcpy(bed.model, "king-flex ff2 dual", sizeof(bed.model));
  strlcpy(bed.rawConfig, "dual yes yes yes no none yes yes yes yes yes yes yes yes no no", sizeof(bed.rawConfig));
  bed.hasLight = true;
  strlcpy(bed.thermal, "none", sizeof(bed.thermal));
  // Values captured from a real FlexFit 2 bed (sides swapped: wire right = on-screen left).
  bed.sleepNumber[SIDE_LEFT] = 45;  bed.ambient[SIDE_LEFT] = 30;  bed.favorite[SIDE_LEFT] = 70;
  bed.sleepNumber[SIDE_RIGHT] = 35; bed.ambient[SIDE_RIGHT] = 30; bed.favorite[SIDE_RIGHT] = 35;
  bed.position[SIDE_LEFT][ACT_HEAD] = 0;   bed.position[SIDE_LEFT][ACT_FOOT] = 0;
  bed.position[SIDE_RIGHT][ACT_HEAD] = 16; bed.position[SIDE_RIGHT][ACT_FOOT] = 0;
  strlcpy(bed.preset[SIDE_LEFT], "flat", 16);
  strlcpy(bed.preset[SIDE_RIGHT], "snore", 16);
  strlcpy(bed.lightRaw, "off 0", sizeof(bed.lightRaw));
  log("SYCG ", "dual yes yes yes no none yes yes ...");
  log("SYAG ", "king-flex ff2 dual");
  bed.version++;
}

}  // namespace SimBed

namespace BedLink {

void begin() {}
void setActive(bool) {}

bool send(const BedCmd& c) {
  char cmd[40];
  switch (c.type) {
    case CmdType::SetSleepNumber:
      snprintf(cmd, sizeof(cmd), "PSNS %s %d", wire(c.side), c.value);
      bed.sleepNumber[c.side] = c.value;
      bed.adjusting[c.side] = true;
      adjustUntil[c.side] = millis() + 6000;
      break;
    case CmdType::SetFavorite:
      snprintf(cmd, sizeof(cmd), "SNFS %s %d", wire(c.side), c.value);
      bed.favorite[c.side] = c.value;
      break;
    case CmdType::Preset:
      snprintf(cmd, sizeof(cmd), "ASTP %s %s", wire(c.side), c.preset);
      strlcpy(bed.preset[c.side], c.preset, 16);
      bed.position[c.side][ACT_HEAD] = strcmp(c.preset, "flat") == 0 ? 0 : strcmp(c.preset, "zero_g") == 0 ? 38 : 16;
      bed.position[c.side][ACT_FOOT] = strcmp(c.preset, "zero_g") == 0 ? 22 : 0;
      bed.moving = true;
      moveUntil = millis() + 4000;
      break;
    case CmdType::SetActuator:
      snprintf(cmd, sizeof(cmd), "ACTS %s %s %d", wire(c.side), c.actuator == ACT_HEAD ? "head" : "foot", c.value);
      bed.position[c.side][c.actuator] = c.value;
      bed.moving = true;
      moveUntil = millis() + 3000;
      break;
    case CmdType::HaltAll:
      strlcpy(cmd, "ACHA ", sizeof(cmd));
      bed.moving = false;
      moveUntil = 0;
      break;
    case CmdType::Light:
      snprintf(cmd, sizeof(cmd), "UBLS %s 15", c.value ? "high" : "off");
      bed.lightOn = c.value;
      strlcpy(bed.lightRaw, c.value ? "high 15" : "off 0", sizeof(bed.lightRaw));
      break;
  }
  printf("  [bed] %s\n", cmd);
  log(cmd, "ok");
  bed.version++;
  return true;
}

BedState snapshot() {
  uint32_t now = millis();
  if (bed.moving && moveUntil && now > moveUntil) {
    bed.moving = false;
    bed.version++;
  }
  for (int s = 0; s < 2; s++)
    if (bed.adjusting[s] && now > adjustUntil[s]) {
      bed.adjusting[s] = false;
      bed.ambient[s] = bed.sleepNumber[s];
      bed.version++;
    }
  if (bed.scanning && now > scanUntil) {
    bed.scanning = false;
    bed.foundCount = 2;
    bed.found[0] = {"12:34:56:78:9a:bc", "Smart bed 789ABC", -64, 0};
    bed.found[1] = {"12:34:56:ab:cd:ef", "Smart bed ABCDEF", -88, 0};
    bed.link = LinkState::Ready;
    bed.version++;
  }
  if (pairReadyAt && now > pairReadyAt) {
    pairReadyAt = 0;
    bed.link = LinkState::Ready;
    strlcpy(bed.status, "Connected", sizeof(bed.status));
    bed.version++;
  }
  return bed;
}

void startScan() {
  bed.scanning = true;
  bed.foundCount = 0;
  bed.link = LinkState::Scanning;
  scanUntil = millis() + 2000;
  bed.version++;
}

void pairWith(const FoundBed& f) {
  strlcpy(bed.bedAddr, f.addr, sizeof(bed.bedAddr));
  strlcpy(bed.bedName, f.name, sizeof(bed.bedName));
  bed.link = LinkState::NeedsPairing;
  strlcpy(bed.status, "Waiting for pairing mode", sizeof(bed.status));
  pairReadyAt = millis() + 5000;
  bed.version++;
}

void forgetPairing() {
  bed.bonds = 0;
  bed.link = LinkState::NeedsPairing;
  strlcpy(bed.status, "Pairing needed", sizeof(bed.status));
  bed.version++;
}

void reconnect() {}

}  // namespace BedLink
