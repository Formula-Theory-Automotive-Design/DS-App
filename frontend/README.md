# frontend

A telemetry dashboard for the backend — a single self-contained `index.html`
with **no build step**. It connects to the backend's WebSocket server and
renders the `telemetry` and `joystick` messages it broadcasts as a motorsport
pit-wall instrument cluster (live link-quality ring gauge, battery readout,
CRSF serial counters, and a real-time control-input view).

The only network dependency is the display/mono webfont pulled from Google
Fonts; it degrades to local condensed/monospace stacks when offline, so the
page still works from a `file://` double-click with no connection.

## Use

1. Start the backend (it serves the WebSocket on port 8080 by default):
   ```sh
   ./build/backend/backend            # add --serial /dev/ttyUSB0 etc. as needed
   ```
2. Open the page. Either just double-click `index.html`, or serve it:
   ```sh
   python3 -m http.server -d frontend 8000   # then visit http://localhost:8000
   ```
3. It auto-connects to `ws://localhost:8080`. Edit the URL field in the header
   to point at another host/port, then Connect.

It reconnects automatically if the backend restarts, and dims a card when its
data goes stale (>1.5 s without an update).

## What it shows

| Card | Source field(s) in the WebSocket JSON |
|------|----------------------------------------|
| Link Quality | `lq` (ring gauge, colour-graded), `rssi` (strength meter), `snr` |
| Battery | `voltage` (large readout), `current`, `capacity`, `remaining` (bar) |
| CRSF Telemetry | `packets`, `crcErrors`, `echoes`, plus live msg/s throughput |
| Control Input | `joystick` message: `axes[]` (centered bars), `buttons[]` (lit cells) |

Gauges colour-grade by threshold (green → amber → red) and each card dims when
its feed goes stale (>1.5 s without an update).

## Editing the configs

The **Configuration** card edits the two hot-reloadable config files live, over
the same WebSocket — no shell or file access needed:

- **Channel Map** (`mapping.conf`) and **Radio / ELRS** (`radio.conf`) each get a
  tab with an editor; a dot marks unsaved changes.
- **Save & Apply** sends the text to the backend, which **validates it by
  parsing first** — a syntax error is reported inline and the file on disk is
  left untouched, so a bad edit can't break the running config. A good save is
  written atomically; the backend's existing mtime watchers then apply it
  (the channel map takes effect on the next TX tick; the radio settings are
  re-pushed to the ELRS module).
- **Reload** re-reads the file from disk; **Revert** discards local edits.

Messages used: `getConfig`/`saveConfig` (client→server), `config`/`saveResult`
(server→client). Only the two known config names are accepted — there is no
arbitrary file access.

The message shapes are produced by `telemetryJson()` / `joystickJson()` in
`backend/main.cpp`. Telemetry fields are omitted until the corresponding
frame type has been received, and the page handles their absence.
