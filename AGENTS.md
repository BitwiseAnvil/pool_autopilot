# Repository Guidelines

## Project Structure & Module Organization

- `atlas-pool-kit/` contains ESPHome sensor firmware, the `components/atlas_pool/` C++ engine, Home Assistant YAML, docs, and font assets.
- `pool-doser/` contains ESPHome master/slave configurations, `pool_doser_rs485.h`, and Home Assistant integration. Wiring references, generators, and images live in `pool-doser/docs/`.
- `home-assistant/` contains the shared HA setup guide, Pool dashboard, Pool Math package and the `pool_tank`/`pool_telemetry` custom integrations.
- Root `components/` holds the shared `pool_clock` and `pool_storage` ESPHome components; root `tests/` covers them and the shared HA code.
- Active regression suites live in `atlas-pool-kit/tests/`, `pool-doser/tests/` and `tests/`; root `tools/` provides shared wrappers.

Read each project's `AGENTS.md` before editing within it. The
[design and safety](docs/design-and-safety.md) document describes the
interlocks, time, recovery and flash-write rules summarized below.

Reviews must never print credential values. The Finder K5-3/K1-3 neutral
branches are a documented known limitation of the reference build: keep the
warning in the wiring documents, and do not propose rewiring the maintainer's
enclosure unless asked.

## Build, Test, and Development Commands

Run from the repository root with Python 3 and `g++` available:

```sh
./tools/esphome config atlas-pool-kit/atlas-pool-kit.yaml
./tools/esphome compile atlas-pool-kit/atlas-pool-kit.yaml
./tools/esphome compile pool-doser/pool-doser.yaml
./tools/esphome compile pool-doser/pool-doser-slave.yaml
./tools/test-atlas
./tools/test-doser
```

`config` validates configuration; `compile` builds firmware without uploading. Test wrappers run each project's regression suite.

Wrappers use `python3` unless `POOL_ESPHOME_PYTHON` names the interpreter that
has the ESPHome version pinned in root `requirements.txt`. Do not create
project-local virtual environments inside the device projects. See `README.md`
for environment setup.

The maintainer works in WSL. If a wrapper fails with `No module named
'esphome'`, find the existing interpreter (`find ~ -maxdepth 4 -path
'*/bin/esphome'`) and set `POOL_ESPHOME_PYTHON` to the `python` beside it. Set
up any tooling a documented step needs (for example the Playwright venv in
`/tmp` for the wiring PDF) and run it rather than skipping the step or asking
first. Windows Chrome at `/mnt/c/Program Files/Google/Chrome/Application/chrome.exe`
can render SVGs headlessly (`--headless=new --screenshot=<windows path>`, using
`wslpath -w` for paths) for visual review. Read manufacturer datasheets
directly: `curl` the PDF and open it with the Read tool.

## Coding Style & Naming Conventions

Use two-space indentation in YAML/C++ and four spaces in Python. Follow existing `snake_case` functions, variables, and ESPHome IDs; `PascalCase` types; and `UPPER_SNAKE_CASE` constants. Keep the Atlas core platform independent. No formatter/linter configuration is checked in; native tests enforce strict compiler warnings.

## Testing Guidelines

Tests use standalone Python assertions with PyYAML/Jinja2 and C++ assertions with address/undefined-behavior sanitizers. Name files `test_*.py` or `test_*.cpp`. No numeric coverage threshold is configured; cover changed behavior and failure paths.

Run the relevant suite after firmware changes. Toolchain upgrades require both suites and all three builds above. For Home Assistant changes, run `./tools/test-atlas --container` and `./tools/test-doser --container`; they use throwaway Docker containers (`POOL_HA_IMAGE` selects the image) and never touch a running installation. Record physical commissioning separately from software checks.

## Commit & Pull Request Guidelines

Only the maintainer reviews and creates Git commits. Agents must never commit or
amend commits on the maintainer's behalf. Requests to fix, deploy, audit, or
clean the repository do not authorize a commit. Leave changes for the
maintainer's review and commit; do not stage changes unless explicitly
requested. Firmware uploads, live Home Assistant changes and physical dose tests
each need explicit permission.

Use focused commits with imperative subjects, matching history: `Add ESPHome integration and testing framework`. PRs should describe behavior changes, link relevant issues, list validation commands/results, and identify hardware verification status. Include screenshots for dashboard or diagram changes.

## Configuration & Safety

Every firmware calendar clock must use `time.nist.gov` only; no other
primary/fallback time servers and no HA service that sets device clocks. The
HA host only needs ordinary automatic time sync; do not require users to
reconfigure their computers. The slave's elapsed-time protection needs no
calendar clock and its Wi-Fi stays off outside maintenance.

Copy adjacent `secrets.example.yaml` files to ignored `secrets.yaml` files and fill credentials locally. Keep secrets, personal addresses and generated build artifacts untracked. Preserve dosing interlocks, fail-off behavior, and safe automatic recovery; Atlas firmware must not command the doser.

Home Assistant setup must only add files, packages, integrations and
dashboards. Never change a user's default dashboard, existing dashboards or
other settings unasked.

## Runtime State and Flash Writes

Operational state must stay in RAM across the doser Master, doser Slave, and
Atlas firmware. Normal operation must cause no recurring flash writes; expected
daily writes should be close to zero. Measurements, averaging windows,
correction flags, relay/fault state, timers, request history, boot counters and
reconnect/recovery activity must not write flash through updates or checkpoints.
Reducing the frequency of such writes does not satisfy this requirement.

There is no device-side dose/tank accounting, rolling 24-hour dose limit or
24-hour restart/unknown-history hold. Do not reintroduce them on the device or
move a cumulative dose limit to HA. The devices keep RAM-only per-dose
quantities, elapsed-time protection and request deduplication; these must not
become cumulative accounting or durable request history.

Ordinary Wi-Fi loss, power loss and restarts must recover automatically once
fresh readings, flow, communication and relay checks pass. Previously enabled
automatic operation must not require manual re-arming after an ordinary restart.
Do not resume an interrupted dose or replay missed doses. Keep existing bounded
flow/mixing waits and physical protections; missing accounting history must
never create a dosing lockout. Preserve deliberate STOP/Auto Off/maintenance
intent and genuine physical-fault protection.

Auto On/Off belongs only to Home Assistant. The restored HA helper
`input_boolean.doser_auto_enabled` is the operator toggle; both On and Off
survive HA restarts. HA applies the 7.80 threshold and requests bounded pulses.
The doser has no saved or RAM Auto mode; it retains fresh observation windows,
request state and physical interlocks. Turning Auto Off only prevents new
automatic requests and never aborts a running dose. HA STOP is the only abort:
it turns the helper Off and sends an immediate abort. Auto/STOP never save
device settings. Device maintenance intent remains a deliberately saved setting.

Audits must include application, ESPHome/framework, vendor-library, and Atlas
sensor nonvolatile writes. Deliberate firmware installation and actual changes
to saved configuration/calibration are the only allowed write categories. Do not
rewrite unchanged settings at boot or reconnect. Any further proposed exception
must identify what is written, its trigger and worst-case frequency, why RAM is
insufficient, and obtain the maintainer's explicit approval. Framework defaults
or wear leveling alone do not justify recurring runtime writes.

The tank estimate is simple: keep capacity and remaining ounces in HA, with
level correction and refill controls. For each manual or automatic dose call,
subtract the full requested ounces only when that call's immediate response
confirms acceptance. A 10 oz accepted request subtracts 10 oz even if interrupted.
A rejected request or missing acceptance response subtracts nothing.

Periodic status, completion, interruption, restart and reconnect never update
the tank. Do not store tank request IDs, boot IDs, sequence numbers, duplicate
markers, dose history or totals. Do not add catch-up or reconciliation. Persist
only capacity and remaining ounces as tank data. The estimate is display-only;
zero or unknown contents never block dosing. Device command replay protection is
a separate safety function and remains RAM-only.

Physical faults latch in RAM while the controller runs; a controller restart
requires fresh relay proving. Master startup discards interrupted requests and
the old minimum-rest countdown. The two-minute pH window, five-minute flow
qualification and five-minute mixing wait rebuild concurrently, with no extra
restart delay. Preserve the 7.80 target and 2 oz automatic pulses.
