Release Notes

- 2.0.18I adds native cumulative battery charge and discharge energy counters for Home Assistant Energy.
- `Battery_Charge_Energy` and `Battery_Discharge_Energy` are published in kWh with `device_class: energy` and `state_class: total_increasing`.
- Energy is integrated locally from actual battery voltage and net battery current, so it no longer depends on a fixed 48 V template.
- The counters continue accumulating while MQTT is offline, survive ESP32 restarts, use alternating verified LittleFS checkpoints, and are flushed before planned restarts.
- Checkpoints are also written when battery flow stops or changes direction, limiting flash wear while keeping the totals durable.

- 2.0.18H fixes Home Assistant long-term statistics for live physical measurements published through MQTT Discovery.
- Battery SOC now publishes `state_class: measurement`, so `sensor.solar2mqtt_battery_percent` can be selected as battery state of charge in the Home Assistant Energy dashboard.
- The same statistics metadata is added consistently to live voltage, current, power, apparent-power, frequency and temperature sensors.

- 2.0.18G adds persistent LittleFS storage for cumulative PV/grid energy counters while MQTT is offline.
- Saved counter samples survive ESP32 reboots and are replayed in order to the normal `LiveData/*` MQTT topics after reconnect.
- Backlog replay is batched, retained topics are finalized with the current live snapshot, and the backlog file is deleted only after a complete successful replay.
- Offline writes are compact binary snapshots, throttled to five-minute intervals (plus immediate offline entry and counter reset detection) to reduce flash wear.
- All supported ESP32 targets now use the shared dual-OTA + 384 KiB LittleFS backlog partition layout.

- Web UI now loads full HTML pages without server-side placeholders; dynamic data comes from `/meta` and `/config`.
- Debug page adds a serial loopback test and a downloadable debug report for current raw/parsed data.
- Favicon is served as `favicon.ico` and the GitHub update check is cached to reduce browser/heap load.
- WebSocket handling and page streaming were optimized to lower heap usage and reduce random page load failures.
- Protocol parsing was expanded to better handle variable inverter response lengths.
