# Changelog

## 1.4.0
- **Apple HomeKit** bridge (HomeSpan), optional:
  - Presets per side ("Alex Zero G") and for both sides ("Zero G") as named switches, so Siri
    works ("Hey Siri, turn on Zero G"), plus the under-bed light.
  - QR-code setup with a code generated on the device; Wi-Fi picker with an on-screen keyboard.
  - A switch acts only on a real on/off change from the Home app. Name updates during setup never
    move the bed.
- **Local API**, optional: JSON over HTTP on port 8080 (`bedremote.local`), advertised over mDNS
  as `_bedremote._tcp` and `_http._tcp`. Presets, firmness, head/foot, light, stop and status.
  See [docs/API.md](docs/API.md).
- Lower RAM use, to fit Wi-Fi next to Bluetooth:
  - Screens and overlays are built on demand.
  - A single draw buffer.
  - NimBLE trimmed to a single client connection.
- Bed presets are cleared when the Bluetooth link sleeps, instead of showing stale values.

## 1.2.0
- First public release: touchscreen remote over local Bluetooth (presets, firmness, head/foot,
  light, stop), side names, bed pairing, bed info, screen timeout, flip screen, UI simulator and
  3D-printable case.
