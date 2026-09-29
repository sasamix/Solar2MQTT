# Solar2MQTT Web Flasher

Static browser flasher for all supported Solar2MQTT ESP32 targets.

The deployment workflow builds fresh full flash images for every PlatformIO
environment, copies them into the GitHub Pages artifact and writes
`version.json` from `platformio.ini`.

The page creates an ESP Web Tools manifest dynamically after the user selects
the exact board. Firmware files are served from the same GitHub Pages origin,
so flashing does not depend on cross-origin access to GitHub Release assets.

Full images intentionally include the partition table and therefore are meant
for first installation/recovery. Normal upgrades of configured devices should
use Solar2MQTT OTA.
