# Repository Guidelines

## Project Structure & Module Organization

- `pool-doser.yaml` implements the Wi-Fi Master; `pool-doser-slave.yaml` implements the RS485 Slave. Shared protocol, RAM request state, and deliberate settings logic lives in `pool_doser_rs485.h`.
- `pool-doser-home-assistant.yaml` supplies the Home Assistant automation package. The installed UI is the repository's Pool dashboard (`../home-assistant/dashboards/pool.yaml`).
- `tests/` contains Python checks, C++ regression programs, and ESPHome stubs in `tests/include/`.
- `docs/` contains build and operation guides, wiring references, generated diagrams, and assets in `docs/images/`; generators live in `docs/tools/`. `BOM.md` lists hardware.

## Build, Test, and Development Commands

Use the ESPHome version pinned in the root `requirements.txt` (see the root
`README.md`); do not create a project-specific virtual environment. The
firmware's minimum supported release is `2026.9.0` for the Master's API
responses and the Slave's encrypted maintenance OTA. From `pool-doser/`:

```sh
test -f secrets.yaml || cp secrets.example.yaml secrets.yaml   # then replace every value
esphome config pool-doser.yaml
esphome config pool-doser-slave.yaml
esphome compile pool-doser.yaml
esphome compile pool-doser-slave.yaml
```

Replace every example value first: ESPHome rejects the all-zeros example keys.
Generate each key with `openssl rand -base64 32`. `config` validates configuration;
`compile` builds firmware for each controller. Build and test commands never
upload firmware.

## Coding Style & Naming Conventions

Use two-space indentation for YAML and C++, and four spaces for Python. Follow surrounding style; no formatter or linter configuration is checked in. Use `snake_case` for functions and ESPHome IDs, `PascalCase` for C++ types, and `UPPER_SNAKE_CASE` for constants. Name regression files `test_*.py` or `test_*.cpp`.

## Testing Guidelines

Tests use Python assertions and C++ assertions with address/undefined-behavior
sanitizers. Run the full suite from the repository root after firmware changes:

```sh
./tools/test-doser
./tools/test-doser --container
```

`--container` runs Home Assistant scenarios in a throwaway container
(`POOL_HA_IMAGE`, default `ghcr.io/home-assistant/home-assistant:stable`) and
must never touch a live Home Assistant configuration. No numeric coverage
threshold is configured; cover changed safety behavior and failure paths.

## Documentation Updates

Edit wiring sources in `docs/wiring-schedule.md`, `docs/tools/wiring_data.py`, and the viewer template `docs/tools/wiring-viewer.html`. Keep the K5-3/K1-3 terminal-rating warning (`TERMINAL_WARNING`) in the sources and every generated output. Regenerate with `python3 docs/tools/build_physical_wiring.py`; follow `docs/tools/README.md` for PDF export and visual review. Keep generated outputs synchronized.

## Commit & Pull Request Guidelines

Use focused commits with imperative subjects. PRs should describe behavior changes, relevant issues, validation commands/results, hardware verification status, and screenshots for dashboard or diagram changes. Record software checks separately from physical measurements.

## Safety & Configuration

This firmware doses acid. Preserve fail-off behavior, controller-enforced
interlocks, and safe automatic recovery. Keep credentials in ignored
`secrets.yaml`.

The Master uses only `time.nist.gov` for calendar time. Preserve independent
elapsed-time dose, flow and mixing protection; the Slave has no clock
dependency. See [design and safety](../docs/design-and-safety.md).

Runtime state stays in RAM. There is no device dose/tank accounting, rolling
24-hour limit or restart/history hold; any reporting belongs in Home Assistant.
Keep per-dose safety timers and command replay protection in RAM. Routine
operation, threshold crossings, reconnects and restarts must not persist
runtime state. Ordinary outages must recover automatically after fresh safety
checks, preserving deliberate operator stops and physical-fault protection.
Physical faults latch only in RAM. Startup discards interrupted requests and
the old rest countdown, rebuilding normal flow/mixing/pH waits concurrently.
Settings save synchronously only when changed; an explicit retry after a failed
save is allowed, with no background retry. A missing settings record is created
only by the deliberate `import_operating_settings` action; single edits never
save displayed defaults.

Auto On/Off is owned and restored by HA, not stored on the doser in RAM or flash.
HA applies the 7.80 threshold to the fresh two-minute average and requests 2 oz
pulses. Keep the doser's observation window, freshness, request tokens and all
physical interlocks. Turning Auto Off prevents new automatic requests but never
aborts a running dose; STOP is the only HA abort. HA STOP turns the helper Off
before sending an immediate abort; the device abort performs no settings save.
Keep deliberate device maintenance persistence. Verify both On and Off
restoration through actual isolated HA restarts.

The HA tank estimate is simple and display-only: keep capacity and remaining
ounces in HA, with level correction and refill controls. For each manual or
automatic dose call, subtract the full requested ounces only when that call's
immediate response confirms acceptance. A 10 oz accepted request subtracts
10 oz even if interrupted; a rejected request or missing acceptance response
subtracts nothing. Periodic status, completion, interruption, restart and
reconnect never update the tank. Do not store tank request IDs, boot IDs,
sequence numbers, duplicate markers, dose history or totals, and do not add
catch-up or reconciliation. Zero or unknown contents never block dosing.

HA keeps **Last Dose** as a template sensor fed by
`sensor.pool_doser_last_delivered`. The **Flow** binary sensor keeps
`device_class: moving`.

Slave Wi-Fi must remain disabled at boot and during normal dosing. Only the
RS485 maintenance lease may enable it after shutdown; releasing lockout requires
a fresh acknowledgment that the radio is off. Do not add a fallback AP or a
safe-mode Wi-Fi override. See [maintenance OTA](docs/slave-maintenance-ota.md)
for the OTA interlock and USB recovery limitation.

Install the Home Assistant side with [the setup guide](../home-assistant/README.md).
Setup must be additive: the Pool dashboard is added to the sidebar and never
made the default screen, and the setup must not change a user's existing
dashboards, defaults or unrelated configuration. Keep Home Assistant
configuration in the repository, not in UI-only settings.
