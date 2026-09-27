# Repository Guidelines

## Project Structure & Module Organization

- `atlas-pool-kit.yaml` configures the ESP32-S3 firmware. `components/atlas_pool/` contains the portable `core.h` engine and the ESPHome/I²C/MQTT adapter.
- `tests/` covers the engine and Home Assistant integration. `docs/` contains the calibration and flashing guides and their images. The Home Assistant package and dashboard YAML live in this folder; fonts are in `fonts/`.

## Build, Test, and Development Commands

Run from the repository root; helpers live in `../tools/`:

```sh
./tools/esphome config atlas-pool-kit/atlas-pool-kit.yaml   # Validate configuration
./tools/esphome compile atlas-pool-kit/atlas-pool-kit.yaml  # Build without uploading
./tools/test-atlas                                              # C++ sanitizers + Python checks
./tools/test-atlas --container                                  # Also validate in a throwaway HA container
```

Wrappers run ESPHome with the Python in `POOL_ESPHOME_PYTHON`, using the version pinned in the root `requirements.txt`. Do not create project-local virtual environments. Toolchain upgrades require all firmware builds and both regression suites described in `../README.md`.

## Coding Style & Naming Conventions

Use two-space indentation in C++/YAML and four spaces in Python. Follow existing `snake_case` functions/variables, `PascalCase` types, and `UPPER_SNAKE_CASE` constants. Keep `core.h` platform independent. No formatter/linter configuration is present; native tests enforce strict compiler warnings.

## Testing Guidelines

Tests use C++ assertions and standalone Python assertions with PyYAML/Jinja2; no numeric coverage threshold is configured. Use `test_*.cpp`/`test_*.py` filenames and descriptive scenario functions. Add regressions for changed behavior, especially stale readings, calibration recovery, and timer rollover. Run `--container` for Home Assistant changes; it needs Docker and uses a throwaway container (`POOL_HA_IMAGE` selects the image) that never touches a live installation. Record physical hardware verification separately from software checks.

## Commit & Pull Request Guidelines

Do not commit, amend, stage or push unless the user explicitly asks. Use short imperative subjects, matching history: `Add ESPHome integration and testing framework`. Keep commits focused. PRs should describe behavior changes, list validation results and hardware verification status, and link relevant issues. Include screenshots for dashboard or display changes. Never upload firmware or change a live Home Assistant installation as part of a code change.

## Security & Configuration

Calendar time must come only from `time.nist.gov`. Preserve the five-minute
SNTP interval, clock health checks, frozen Unix measurement timestamps and the
HA freshness validator described in [time and recovery](../docs/design-and-safety.md).

Copy `secrets.example.yaml` to `secrets.yaml` in this folder for local configuration. Keep credentials, OTA keys, and generated artifacts untracked. Atlas is a sensor monitor; keep dosing controls outside this project and never command the doser.

Follow the [runtime state and recovery requirements](../docs/design-and-safety.md).
Measurements, session progress, recovery messages and connection state belong
in RAM. Normal monitoring, reconnects and restarts must not rewrite ESP32 or EZO
nonvolatile storage. Save calibration/configuration only for deliberate changes;
do not resend nonvolatile setters automatically for unchanged settings. Normal
monitoring must resume automatically after ordinary outages. A deliberate
calibration/maintenance session must still prevent its readings from authorizing
dosing; an ordinary monitoring restart must not invent such a session or demand
manual probe confirmation.

Only maintenance intent and deliberately changed reference numbers persist on
the ESP32. Calibration progress and procedure selection are RAM-only; after a
restart inspect actual EZO calibration status without replaying commands.
Initialization uses read-only RTD scale/logging and EC output queries. Persistent
setters belong only in explicitly confirmed configuration/calibration operations.
The shared `pool_storage` adaptation is version checked; do not patch the shared
ESPHome installation or SDK caches. See the [write inventory](../docs/design-and-safety.md).
