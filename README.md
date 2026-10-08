# Bartender

Firmware and web interface for a bartending robot.

- `firmware/`: ESP32-S3 firmware (PlatformIO)
- `web/`: SvelteKit web interface, served from the machine's SD card
- `sdcard/`: SD card config and recipe templates

Tasks are defined in `mise.toml`. Run `mise tasks` to list them.
