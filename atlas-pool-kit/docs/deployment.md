# Flashing and updating

The [README](../README.md) has the normal build, first-install and update
commands. This guide covers the details: download mode, a full erase, ESPHome
Web, Wi-Fi updates and recovery. Commands run from the repository root.

Firmware images contain your Wi-Fi and MQTT credentials. Keep them private.

## Build output

```sh
./tools/esphome compile atlas-pool-kit/atlas-pool-kit.yaml
```

ESPHome writes these to `atlas-pool-kit/.esphome/build/atlas-pool-kit/build/`:

| File | Use |
| --- | --- |
| `firmware.factory.bin` | Full image (bootloader, partition table, app) for a USB install at offset `0x0` |
| `firmware.ota.bin` | Application only, for Wi-Fi updates |
| `firmware.elf` | Debug symbols |

Never install the factory image through a Wi-Fi updater. The 4 MB layout has two
1.75 MB (`0x1C0000`) application slots and an NVS partition. If you add features
or change framework versions, check that the image still fits.

## Before the first USB install

1. Confirm the board is the Adafruit Feather ESP32-S3 TFT with 4 MB flash and
   note its serial port. Close any serial monitor that holds the port.
2. Check that the EZO circuits are connected at 99 (pH), 98 (ORP), 102 (RTD) and
   100 (EC). Flashing the Feather does not erase or reset the EZO circuits; their
   calibration is kept.
3. If you want a record, note each circuit's calibration status, EC probe K and
   TDS factor from its current firmware. This is optional.

## Download mode

If the upload cannot connect, put the ESP32-S3 in ROM download mode: hold
**BOOT** (D0), press and release **RESET**, then release **BOOT**. The USB
serial port can disappear and come back under a different name at this point,
so check the port again. See
[Adafruit's pinout guide](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/pinouts)
for the button locations.

## First installation

**ESPHome over USB.** For a board that already runs ESPHome or is blank:

```sh
./tools/esphome upload atlas-pool-kit/atlas-pool-kit.yaml --device /dev/ttyACM0
```

**Full erase first.** If the board previously ran other firmware (for example
the kit's original Arduino sketch or a UF2 bootloader), erase it so no old
partition table or settings remain. ESPHome installs `esptool` with it:

```sh
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 erase-flash
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --after watchdog-reset \
  write-flash 0x0 atlas-pool-kit/.esphome/build/atlas-pool-kit/build/firmware.factory.bin
```

Use the same Python that has ESPHome installed. On Windows, the port is `COMx`.

**ESPHome Web.** Alternatively, open [web.esphome.io](https://web.esphome.io)
in Chrome or Edge, connect the board and install `firmware.factory.bin`.

**Leaving download mode.** When the board was put into download mode by hand,
a normal reset after flashing can leave it waiting for another download.
`--after watchdog-reset` avoids this; otherwise press **RESET** once. See
[Espressif's note on USB-Serial/JTAG download mode](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html#leaving-download-mode-in-usb-serial-jtag-mode).
The port may briefly vanish during that reset; that is not a failed flash if
the write already passed hash verification.

## After the first boot

Watch the log (`./tools/esphome logs atlas-pool-kit/atlas-pool-kit.yaml`) or
the display and check that:

- all four circuits are found and readings appear on the TFT,
- the display orientation and colors look right,
- the **Atlas Pool Kit** device appears in Home Assistant under MQTT, and
- the diagnostics show a healthy clock.

Opening a serial monitor can reset the board; check uptime after closing it.
Reserve the kit's IP address in your router so Wi-Fi updates have a fallback
when mDNS does not resolve. Then calibrate when ready, using the guides.

## Wi-Fi updates

Updates use ESPHome's encrypted OTA on TCP port 3232 with the same
`atlas_ota_key`. They do not depend on the MQTT broker.

1. Finish any calibration and return every probe to pool water. Resolve any
   pending recovery first.
2. Build and upload:

   ```sh
   ./tools/esphome upload atlas-pool-kit/atlas-pool-kit.yaml --device atlas-pool-kit.local
   ```

   Replace the hostname with the kit's IP address if mDNS does not resolve (it
   often does not across WSL, VPNs or separate VLANs).

If an update starts during calibration, the firmware disarms writes and clears
queued actions. A calibration write already sent to a circuit may or may not
have been accepted; check the calibration status afterwards. Calibration is
never replayed automatically.

### Check the update stack budget

If you change the ESPHome adapter, check the OTA callback's stack use in the
compiled firmware before uploading. Pass the ESP32-S3 `objdump` from the
toolchain ESPHome installed (under `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/`
by default) if it is not on your `PATH`:

```sh
python3 atlas-pool-kit/tests/test_firmware_stack.py \
  atlas-pool-kit/.esphome/build/atlas-pool-kit/build/firmware.elf \
  --objdump ~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-objdump
```

## Recovery

- After repeated failed boots, ESPHome safe mode starts only Wi-Fi and OTA, so
  you can upload a fixed image over Wi-Fi. It is a recovery aid,
  not a guarantee against every bad image.
- ROM download mode is always available over USB: enter it as described above
  and reinstall the factory image. Keep USB access to the kit possible.
- Test a wrong OTA key, an interrupted upload and a following successful upload
  when commissioning, so you know recovery works before you need it.
