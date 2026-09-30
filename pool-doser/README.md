# Pool Doser

ESPHome firmware and build documentation for a two-controller acid dosing
pump (built around a 120 V Stenner peristaltic pump). A Wi-Fi **Master**
controls Relay A and talks to Home Assistant; an RS485 **Slave** controls
Relay B and senses outlet voltage. Each relay drives its own motor-rated Finder
contactor, and the two contactors' power contacts are in series, so either
controller can cut pump power even if the other contactor welds. Home
Assistant may request a dose, but it cannot bypass the controllers' safety
checks.

**This project doses acid.** Test with water only, keep chemical out of every
fault test, and have the 120 V wiring reviewed by someone qualified.

## Safety summary

- Both relays open at boot, abort, RS485 loss and watchdog expiry. Every dose
  proves A-open/B-closed, A-closed/B-open, then both closed with outlet voltage
  present before the timed run.
- Protocol v5 binds energizing commands to both current controller sessions and
  a staged transaction. A restart ends any delivery and invalidates old commands.
- Confirmed relay isolation faults, contradictory outlet voltage and failed
  shutdown latch in RAM. Communication loss and peer restarts recover through
  fresh status and relay proving; reconnection never clears a physical fault.
  **Clear Latched Relay Fault** recovers deliberately under maintenance.
- Every dose needs flow, a valid calibration, the single-dose bound, the
  independent 15-minute runtime ceiling, completed rest, healthy communication
  and safe relay feedback. Flow loss aborts after the 500 ms filter.
- Runtime state stays in RAM. Only deliberately changed settings and
  maintenance intent are saved; routine operation, restarts and reconnects
  write no flash. There is no device dose accounting, cumulative limit or
  history hold.
- Ordinary Wi-Fi loss, power loss and restarts recover automatically once
  fresh readings, flow, communication and relay checks pass. An interrupted
  dose is never resumed and a missed dose is never replayed.
- **Maintenance Lockout** opens both outputs, then lets the Slave enable Wi-Fi
  for encrypted OTA. Releasing it waits for a fresh radio-off acknowledgment.
  The Slave's Wi-Fi is otherwise off; there is no fallback access point.

The shared rules for runtime state, flash writes and time are in
[design and safety](../docs/design-and-safety.md).

## Hardware

See the [bill of materials](BOM.md). In brief: two Waveshare
ESP32-S3-Relay-1CH-U controllers, two Finder 22.32.0.120.1320 contactors, a
Phoenix Contact 2966281 outlet-live detector, a Mean Well MDR-20-5 5 V supply
(it replaced an HDR-15-5 that added noise to the Atlas pH reading),
a Schneider 5 A C60 breaker, a normally-open paddle flow switch, a 270 Ω
pull-up resistor and 14/18 AWG hookup wire. The Master and Slave share an
intact Cat6A twisted pair for RS485 with both onboard 120 Ω terminators on.

## Wiring book

The enclosure uses three harnesses: **A** black/white/green supply, **B** red
switching and **C** low voltage, with 28 wire IDs and 56 ends. Every 120 V
wire is stranded 14 AWG; shared ends are one ferrule at an existing device
screw; exactly four leads cross into the metal junction box, including green
A17 from the ground Wago to the Mean Well FG terminal.

> [!WARNING]
> The neutral branches at Finder K5 terminal 3 (four 14 AWG wires) and K1
> terminal 3 (three) exceed Finder's rated terminal capacity. They document
> the reference build as built; do not copy them. Use a rated DIN-rail neutral
> terminal block instead. See the [wire schedule](docs/wiring-schedule.md).

- [Color wiring viewer](docs/physical-wiring.html) and
  [printable PDF](docs/pooldose-physical-wiring.pdf)
- [Assembly instructions and checkout](docs/build-instructions.html)
- [Wire schedule](docs/wiring-schedule.md) (the source of truth),
  [both-end termination list](docs/termination-schedule.md) and
  [bench cut list](docs/harness-cut-list.md)
- Single sheets: [overview](docs/pooldose-overview-wiring.svg),
  [black hot](docs/pooldose-mains-wiring.svg),
  [white neutral](docs/pooldose-neutral-wiring.svg),
  [red switching](docs/pooldose-switched-wiring.svg),
  [+5 V](docs/pooldose-dc-wiring.svg),
  [DC return/data](docs/pooldose-returns-wiring.svg),
  [GPIO](docs/pooldose-signals-wiring.svg),
  [shared ferrules](docs/pooldose-ferrules-wiring.svg) and the
  [combined book](docs/pooldose-wiring-diagram.svg)
- [Enclosure reference photo](docs/images/enclosure-2026-09-18.jpg) and
  [hole layout](docs/Hole%20Layout.pdf)

The generators and PDF export are described in [docs/tools](docs/tools/README.md).

| Circuit | Connection |
|---|---|
| Pump power | F1 protected hot → K1 1–2 → K5 1–2 → switched output → receptacle → pump |
| Master coil | F1 hot → Master relay COM/NO → K1 A1; K1 A2 → neutral |
| Slave coil | F1 hot → Slave relay COM/NO → K5 A1; K5 A2 → neutral |
| Controller supply | F1 hot/neutral → Mean Well L/N; box ground → Mean Well FG; +V/−V → both controllers' 5 V/GND |
| Outlet detector | K4 A1/A2 across the final switched output; its 11–14 contact between Slave GPIO1 and GND, with 270 Ω from GPIO1 to 3.3 V |
| Flow switch | Normally-open contact between Master GPIO1 and GND (internal pull-up); an open cable reports no flow |
| RS485 | Master A+ ↔ Slave A+, B− ↔ B− on one intact twisted pair |

The pump line, both contactor coils, the detector and the 5 V supply must all
come from the load side of F1 and the same upstream protected branch and
disconnect, with GFCI protection upstream of the sensed output. The Waveshare
relays carry only coil current, never pump current. A welded channel leaves
the other able to stop the pump; this does not cover two welded contactors,
wiring bypasses or common control failures. A shorted flow-switch cable can
falsely report flow, so protect the field cable and recheck the no-flow state
periodically.

Mount the flow switch in the shared return after the filter/heater, upstream
of chemical injection and any pool/spa split, with its arrow in the flow
direction. Allow at least 12 inches of straight pipe upstream and 4 inches
downstream, then place the injection fitting as the last shared-pad item.
Confirm the lowest circulation program that permits dosing stays above the
switch's measured turn-on point.

## Build and flash

The combined repository pins ESPHome in its root `requirements.txt`; see the
[root README](../README.md) for the environment and the `../tools/esphome`
wrapper. From `pool-doser/`:

```sh
cp secrets.example.yaml secrets.yaml   # then replace every value
esphome config pool-doser.yaml
esphome config pool-doser-slave.yaml
esphome compile pool-doser.yaml
esphome compile pool-doser-slave.yaml
```

`secrets.yaml` is ignored by Git. It holds the Wi-Fi credentials, the
Master's `api_encryption_key` and the Slave's independent `slave_ota_key`
(each a random 32-byte Base64 value, for example `openssl rand -base64 32`).
Keep `pool_doser_rs485.h` and `pool_doser_control.h` beside the YAML files.

- **Master:** flash `pool-doser.yaml` over USB the first time. Later updates
  use encrypted OTA with the same `api_encryption_key` the API uses; there is
  no separate OTA password.
- **Slave:** flash `pool-doser-slave.yaml` over USB the first time. Later
  updates use encrypted maintenance OTA: turn **Maintenance Lockout** on, wait
  for the Slave to join Wi-Fi, then upload. See
  [Slave maintenance OTA](docs/slave-maintenance-ota.md) for the lease,
  recovery and signal diagnostics.

Always install both controllers from the same source revision; controllers
with different RS485 protocol versions ignore each other and never dose.
Neither target has a web server, captive portal or fallback access point. The
Master keeps calendar time from `time.nist.gov`; the Slave needs no clock.

## Configuration and calibration

The Master's Configuration panel holds **Pump Rating** (the pump's labeled
GPD), **Calibration Factor**, **Max Single Dose** (ounces), **Pre-Run Seconds**
and **Minimum Rest Seconds**. A new Master has no saved settings and refuses
to dose until you create them once with the
`esphome.pool_doser_import_operating_settings` action while Maintenance Lockout
is on; afterward, edit the numbers normally. Each value saves only when it
changes.

Run time in minutes is
`requested ounces × 11.25 ÷ Pump Rating × Calibration Factor`, capped by the
independent 15-minute ceiling. Keep Pump Rating at the label and adjust the
factor from collected water. Pre-run is an optional delay before relay
proving; minimum rest applies after a dose and is never saved. See
[pump calibration](docs/pump-calibration.md) for first configuration and the
water measurement procedure.

## Home Assistant

Install the Home Assistant side with the
[setup guide](../home-assistant/README.md). It adds
`pool-doser-home-assistant.yaml` as a package, the `pool_tank` component and a
**Pool** dashboard in the sidebar; it never changes existing dashboards or the
default screen.

- **Auto Enabled** (`input_boolean.doser_auto_enabled`) is the only Auto
  switch. Home Assistant restores both On and Off across its restarts; a new
  installation starts Off. The doser has no Auto setting.
- Turning Auto Off stops new automatic requests but never aborts a running
  dose. **STOP** (`script.doser_stop`) turns Auto Off and immediately aborts;
  it is the only Home Assistant abort.
- The tank estimate is display-only. Home Assistant stores capacity and
  remaining ounces and subtracts a dose's full requested ounces only when that
  call's immediate response confirms acceptance. Zero or unknown contents never
  block dosing.
- **Last Dose** is a Home Assistant template sensor fed by
  `sensor.pool_doser_last_delivered`.

## Automatic acid control

Home Assistant forwards Atlas pH readings (MQTT topics under
`pool/atlas_pool/`) to the Master, which keeps a fresh, time-weighted
two-minute average. When Auto is on and that average is **above 7.80**, Home
Assistant requests one **2 oz** pulse at a time; at or below 7.80 it requests
nothing. Startup needs five minutes of continuous flow and five minutes of
mixing, running concurrently with the pH window. Later doses also wait for
mixing plus the configured minimum rest. An accepted pulse can finish during a
Home Assistant, Wi-Fi or MQTT outage under local protection. The Atlas sensor
never commands the doser.

See [automatic acid control](docs/automatic-acid-control.md) for freshness
rules, recovery behavior and the action/response contract.

## Before chemical service

Work through these with water only and the pump's chemical line out of the
tank. Record each measurement; simulated tests do not replace them.

1. Record pump full-load and starting current, contactor ratings, branch and
   receptacle ratings, protection, grounding and conductor ratings. Confirm
   the pump line, both contactor coils, the detector and the 5 V supply all
   lose power from the same upstream disconnect.
2. Follow the checkout in the [assembly instructions](docs/build-instructions.html):
   5.00 V at the supply output with all relays closed, continuity and
   separation, and the GPIO resistance checks.
3. Meter the flow switch open at rest and closed only while the paddle is
   deflected. With water, prove it reports no flow with circulation off and
   flow with it on; disconnecting either switch wire must report no flow.
   Verify flow loss aborts an active water test.
4. With the pump unplugged and a safe test load connected, prove the three
   relay states and verify contradictory states latch a fault. With all power
   disconnected, simulate each contactor's power path stuck closed in turn and
   verify the other one still interrupts the load; remove the simulation
   afterward.
5. Confirm communication, outlet sensing, a normal timed run and STOP, then
   calibrate with water as in [pump calibration](docs/pump-calibration.md).
6. During a water-only run, interrupt RS485 and each controller's power.
   Verify shutdown and automatic recovery after fresh checks, without a false
   physical-fault latch.

## Testing

From the repository root:

```sh
./tools/test-doser              # protocol, runtime, wiring and HA package checks
./tools/test-doser --container  # adds Home Assistant scenarios in Docker
```

The runtime tests compile the production Master scripts and Slave control
lambdas with simulated contacts, elapsed time, RS485 and storage, under
address and undefined-behavior sanitizers. They cover bounded delivery,
interruptions, session replay, relay faults, deliberate saves, maintenance
leases, OTA blocking, stale frames, long-running monitoring and timer
rollover. `--container` runs Home Assistant scenarios in a throwaway
`ghcr.io/home-assistant/home-assistant:stable` container (override with
`POOL_HA_IMAGE`); it mounts no configuration. Also run the four `esphome`
commands above after any firmware change. Software checks do not verify
physical wiring, flash behavior or chemical delivery.
