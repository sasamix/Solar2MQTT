Release Notes

- 2.0.18I cleans up Home Assistant MQTT Discovery and localizes Solar2MQTT entity names to Russian.
- All explicitly supported static/live Solar2MQTT sensors now publish Russian display names while existing entity_id/unique_id values remain unchanged.
- ESP32 internal temperature and DS18B20 discovery names are localized to Russian.
- PowMr control entity names are localized while command/state payload compatibility is preserved.
- Generic HA discovery now publishes only explicitly catalogued entities; runtime diagnostic/probe fields are no longer exposed as sensors.
- On MQTT reconnect the firmware performs a short retained Discovery sweep scoped to its own device and removes stale entities that are not part of the current supported catalog, including old debug/probe entities no longer present in runtime data.
- Battery charging/discharging power and cumulative battery energy sensors from 2.0.18H are preserved unchanged.

- 2.0.18H adds native cumulative battery charge/discharge energy counters and fixes Home Assistant long-term statistics for live physical measurements published through MQTT Discovery.
- Native `Зарядка батареи` / `Разрядка батареи` sensors are published in kWh with `device_class: energy` and `state_class: total_increasing`; their requested entity IDs are `sensor.zariadka_batarei` and `sensor.razriadka_batarei` for migration from existing HA helpers without losing Recorder statistics.
- Battery energy is integrated locally from actual battery voltage and net battery current, so it no longer depends on a fixed 48 V template.
- Native calculated `sensor.battery_charging_power` and `sensor.battery_discharging_power` replace the previous HA template power sensors and use actual battery voltage.
- The counters continue accumulating while MQTT is offline, survive ESP32 restarts, use alternating verified LittleFS checkpoints, and are flushed before planned restarts.
- Checkpoints are also written when battery flow stops or changes direction, limiting flash wear while keeping the totals durable.
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
