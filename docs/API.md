# Local API

While Wi-Fi is on (**Settings → HomeKit → Apple Home**), the remote also serves a small JSON API
on your home network. Anything on the same network can use it: scripts, Home Assistant, Node-RED,
iOS Shortcuts, another ESP32, a browser. HomeKit pairing is not needed for the API, only Wi-Fi.

- Address: `http://bedremote.local:8080` (or the IP shown at the bottom of the HomeKit screen)
- Turn it off with **Settings → HomeKit → Local API**. It is on by default.
- There is **no authentication**. Anyone who can reach your network can move the bed. Turn the
  API off if your network has devices or guests you don't trust.

## Discovery
The remote advertises over mDNS / Bonjour / Zeroconf:

| Service | Port | TXT |
|---|---|---|
| `_bedremote._tcp` | 8080 | `path=/api`, `fw=<version>` |
| `_http._tcp` | 8080 | `path=/api` |
| `_hap._tcp` | 80 | HomeKit (not part of this API) |

Host name: `bedremote.local`. For example:

```bash
dns-sd -B _bedremote._tcp            # macOS
avahi-browse -rt _bedremote._tcp     # Linux
```

## Conventions
- Every endpoint accepts **GET or POST**. Parameters can be in the query string, a form body
  (`application/x-www-form-urlencoded`), or a flat JSON object. These are all the same:
  ```bash
  curl "http://bedremote.local:8080/api/preset?side=left&preset=zero_g"
  curl -d "side=left&preset=zero_g" http://bedremote.local:8080/api/preset
  curl -H "Content-Type: application/json" -d '{"side":"left","preset":"zero_g"}' \
       http://bedremote.local:8080/api/preset
  ```
- `side` is `left`, `right`, `both` (the default when omitted), or a side's name as set on the
  remote (case-insensitive). Left and right are as on the remote's screen, so they follow
  **Swap sides**.
- Replies are JSON with `"ok": true`, or `"ok": false` and an `"error"` message along with HTTP
  400 (bad parameter), 404 (unknown endpoint), 405 (method), 413 (request over 1.5 KB) or
  503 (command queue full, try again).
- CORS is open (`Access-Control-Allow-Origin: *`), so a web page on your network can call it.
- One request at a time, one request per connection. Keep polling to every few seconds or slower.

### The bed link
When the remote's screen is off it disconnects from the bed so the SleepIQ app can connect. A
command reconnects it for a minute and is sent as soon as it's connected (usually 1–3 s). Command
replies mean "queued", not "done"; with the link asleep they include a `note`.

## Endpoints

### `GET /api`
Lists the endpoints.

### `GET /api/status`
```json
{
  "ok": true,
  "device": "Bed Remote",
  "firmware": "1.4.0",
  "link": "ready",
  "connected": true,
  "moving": false,
  "model": "king-flex ff2 dual",
  "presets": ["flat", "zero_g", "snore", "favorite"],
  "light": {"on": false},
  "sides": [
    {"side": "left", "name": "Alex", "sleepNumber": 45, "favorite": 45, "preset": "flat", "head": 0, "foot": 0},
    {"side": "right", "name": "Sam", "sleepNumber": 40, "favorite": 35, "preset": "zero_g", "head": 12, "foot": 20}
  ]
}
```
- `link`: `asleep`, `no_bed`, `scanning`, `connecting`, `needs_pairing`, `busy`, `ready` or `error`.
- While `connected` is false, values are from the last connection and `preset` is `null` (the bed
  may have been moved since). Add `?refresh=1` to wake the link, then ask again a few seconds later.
- `light` is `null` if the bed has no under-bed light. `null` values are not known yet.
- `head` / `foot` are 0–100.

### `/api/preset`
| Parameter | Values |
|---|---|
| `side` | `left`, `right`, `both`, or a name |
| `preset` | `flat`, `zero_g`, `snore`, `favorite`, `watch_tv`, `read` (as listed in `status.presets`) |

Also accepts `zero-g`, `Zero G`, `anti_snore`, `tv`.

### `/api/sleep-number`
| Parameter | Values |
|---|---|
| `side` | `left`, `right`, `both`, or a name |
| `value` | 5–100, rounded to a multiple of 5 |

The bed then takes a while to inflate or deflate; `status.sides[].sleepNumber` shows the setting.

### `/api/position`
| Parameter | Values |
|---|---|
| `side` | `left`, `right`, `both`, or a name |
| `actuator` | `head` or `foot` |
| `value` | 0–100 |

### `/api/light`
| Parameter | Values |
|---|---|
| `on` | `true` / `false` (also `1`/`0`, `on`/`off`) |

The light turns itself off after the timer in `include/config.h` (`LIGHT_TIMER_MIN`).

### `/api/stop`
Stops all motion on both sides.

## Examples

### Shell
```bash
curl -s http://bedremote.local:8080/api/status | jq '.sides[] | {name, preset, sleepNumber}'
curl -d "side=both&preset=flat" http://bedremote.local:8080/api/preset
curl -d "side=Sam&value=50" http://bedremote.local:8080/api/sleep-number
curl -X POST http://bedremote.local:8080/api/stop
```

### Python
```python
import requests

BED = "http://bedremote.local:8080/api"
requests.post(f"{BED}/preset", json={"side": "left", "preset": "zero_g"}).raise_for_status()
print(requests.get(f"{BED}/status").json()["sides"][0]["preset"])
```

### Home Assistant
```yaml
# configuration.yaml
rest_command:
  bed_preset:
    url: "http://bedremote.local:8080/api/preset"
    method: post
    content_type: "application/json"
    payload: '{"side": "{{ side }}", "preset": "{{ preset }}"}'
  bed_stop:
    url: "http://bedremote.local:8080/api/stop"
    method: post

rest:
  - resource: "http://bedremote.local:8080/api/status"
    scan_interval: 60
    sensor:
      - name: "Bed left preset"
        value_template: "{{ value_json.sides[0].preset }}"
      - name: "Bed left sleep number"
        value_template: "{{ value_json.sides[0].sleepNumber }}"
```
Call it from an automation with `service: rest_command.bed_preset` and
`data: {side: both, preset: flat}`.

### iOS Shortcuts
Use **Get Contents of URL** with `http://bedremote.local:8080/api/preset?side=both&preset=flat`.

## Notes
- If `bedremote.local` doesn't resolve on your network, use the IP address. Reserving it in your
  router keeps it stable.
- The API shares the ESP32's memory with Wi-Fi, Bluetooth and HomeKit. Each request uses about
  4 KB while it's being handled and nothing while idle.
- Implementation: [`src/net_api.cpp`](../src/net_api.cpp).
