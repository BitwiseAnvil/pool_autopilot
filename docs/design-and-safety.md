# Design and safety

This document explains how the Atlas Pool Kit, the Pool Doser and Home Assistant
(HA) work together, and the rules that keep automatic acid dosing safe. It
applies to the doser Master, the doser Slave, the Atlas ESP32, their
frameworks and libraries, the attached Atlas EZO circuits and the HA package.

Read the warning in the [repository README](../README.md) first. These rules
reduce risk; they do not replace your own review, testing and supervision.

## Roles

| Part | Job | Never does |
| --- | --- | --- |
| Atlas Pool Kit | Measures pH, ORP, temperature, conductivity, salinity and TDS; publishes timestamped readings over MQTT | Command the doser |
| HA `pool_telemetry` | Admits only fresh, trusted readings and republishes them as live values | Store or replay readings |
| HA package | Owns Auto On/Off, applies the 7.80 pH threshold, requests bounded pulses, keeps the tank estimate | Bypass the doser's checks |
| Doser Master (Wi-Fi) | Averages pH, checks flow, rest and clock, drives Relay A, supervises the Slave over RS485 | Keep Auto state or dose history |
| Doser Slave (RS485) | Drives Relay B, senses outlet voltage, enforces its own runtime deadlines | Use Wi-Fi outside maintenance |

Relays A and B each drive a separate motor-rated contactor, and the two power
contacts are in series. Either controller can therefore cut pump power even if
the other contactor welds shut.

## Dosing interlocks

Every dose, manual or automatic, must pass the same checks:

- **Fail off.** Both relays open at boot, on abort, on RS485 loss and on
  watchdog expiry.
- **Relay proving.** Before each dose the controllers prove A-open/B-closed,
  A-closed/B-open, then both closed with outlet voltage present, and verify
  shutdown afterwards.
- **Flow.** The flow switch must show continuous flow; flow loss stops a dose.
- **Bounds.** A per-dose size limit, valid pump calibration, an independent
  15-minute runtime ceiling enforced by the Slave, and a minimum rest between
  doses.
- **Sessions.** Energizing commands are bound to the current Master and Slave
  boot sessions and a staged transaction. A restart ends any delivery in
  progress and invalidates old commands; retries cannot restart or extend a
  pulse.
- **Physical faults.** A confirmed relay isolation or shutdown failure, or
  contradictory outlet feedback, latches in RAM. Reconnecting does not clear
  it. Clearing requires deliberate maintenance recovery, or a power cycle
  followed by fresh relay proving.
- **Maintenance.** Maintenance opens the outputs before the Slave's Wi-Fi is
  allowed on for updates, and releasing it waits for the Slave to confirm its
  radio is off again.

There is no cumulative daily dose limit, no device-side dose accounting and no
restart or unknown-history hold. Single-dose bounds are separate from any daily
total. Missing history can never lock out dosing, and it can never permit an
unsafe dose.

## Automatic control

- **HA owns Auto.** The toggle is the HA helper
  `input_boolean.doser_auto_enabled`. HA restores both On and Off after its own
  restart; a new installation starts Off. The doser has no Auto setting in RAM
  or flash.
- **Threshold and pulse size.** HA requests one **2 oz** pulse at a time only
  while the fresh two-minute average pH is **above 7.80**. At or below 7.80 it
  requests nothing more.
- **Waits.** Automatic dosing needs five minutes of continuous flow, five
  minutes of mixing and a fresh two-minute pH window. After a restart these
  rebuild at the same time, with no extra delay. Each later dose also waits
  for mixing plus the configured minimum rest.
- **One-use tokens.** HA asks the Master for status; only a ready Master issues
  a token that expires after ten seconds. The automatic dose call consumes the
  token and the Master rechecks every prerequisite. Stale, consumed and
  previous-boot tokens cannot start the pump.
- **Auto Off does not abort.** Turning Auto Off only prevents new automatic
  requests. A pulse already accepted, automatic or manual, finishes under its
  normal bounds and local protection.
- **STOP is the only abort.** The HA STOP script turns Auto Off and immediately
  aborts the dose. The abort opens the outputs first and saves nothing on the
  device.

## Time and fresh readings

Calendar clocks in firmware use **`time.nist.gov` only**. ESPHome configuration
validation rejects any other server or a sync interval other than five minutes.
Do not add fallback servers or a time source in HA that sets device clocks.

- Atlas and the Master sync at startup, after Wi-Fi reconnects (at most once per
  15 seconds) and every five minutes. Time is untrusted after boot until a real
  sync succeeds. After 30 minutes without a sync, clock health expires and new
  automatic decisions pause. A clock step over two seconds discards the current
  observation window.
- The Slave has no calendar clock and keeps Wi-Fi off. Pump runtimes and the
  flow, mixing and rest waits use elapsed time, so correcting the calendar
  clock can never lengthen a dose or shorten a wait.
- Each Atlas reading on `pool/atlas_pool/reading/<key>` carries its value, its
  acquisition time `measured_at_ms` (64-bit Unix milliseconds), the device boot
  ID and a clock epoch that changes when clock continuity breaks. A reconnect
  never re-dates an old sample.
- `pool_telemetry` accepts readings under 20 seconds old and diagnostics under
  10 seconds old, with at most two seconds of clock lead. It uses monotonic
  deadlines, so duplicate, retained, reordered or delayed messages cannot renew
  a reading, and readings expire even when no further message arrives.
- It compares timestamps against the HA host's own clock. Keep the HA host on
  normal automatic time sync; if its clock drifts by more than a few seconds,
  readings are rejected and automatic dosing pauses rather than acting on
  doubtful data.
- The Master independently checks each forwarded sample's timestamp against its
  own NIST clock and expires HA's heartbeat after 15 seconds.
- Telemetry uses MQTT QoS 0, no retain and a 10-second message expiry. Keep the
  Mosquitto default `queue_qos0_messages false`. Nothing is queued or replayed;
  gaps in history during an outage are intentional.

NIST is an internet service, so a long internet outage pauses automatic dosing
even while the local network works. That is the intended safe behavior.

## Automatic recovery

Ordinary Wi-Fi loss, power loss and restarts recover without anyone present:

1. Outputs start or return off. An interrupted pulse is over: it is never
   resumed, and missed doses are never replayed.
2. Old samples, tokens and command authorizations are discarded, along with the
   Master's old minimum-rest countdown.
3. Fresh communication, safe relay feedback, usable calibration, current flow
   and trusted readings are re-established, and the normal waits rebuild in RAM.
4. If Auto is On in HA, automatic dosing resumes once those checks pass. No
   manual re-arming is needed after an ordinary outage.

Recovery never overrides deliberate intent or a real fault. STOP, Auto Off and
maintenance stay in force until changed on purpose, and a latched physical fault
stays latched while that controller runs. Communication loss alone, including
the Master losing power and opening Relay A before the Slave times out, is an
interruption, not proof of a failed relay.

| Event | Result |
| --- | --- |
| Master restart | Outputs off; old request, token, result and rest countdown discarded; maintenance restored; fresh checks rebuild |
| HA restart | Auto restored On or Off; new observer session and new pH window before any request |
| Slave restart or RS485 loss | Delivery ends; sessions rediscovered; recovery after fresh status and relay proving |
| HA, API, Wi-Fi or MQTT outage | An accepted pulse may finish locally; new requests wait for fresh readings |
| No flow, stale readings, bad calibration or unhealthy clock | Wait until that prerequisite actually returns |
| Unknown or empty tank estimate | Display only; dosing unaffected |

## Runtime state stays in RAM

Normal operation causes **no recurring flash writes**; expected daily writes are
close to zero. Measurements, averaging windows, correction flags, relay and
fault state, timers, request results and deduplication, boot counters, recovery
activity and calibration-session progress all live in RAM. Writing them less
often would not meet this rule; they are not written at all.

Only two kinds of nonvolatile write are allowed:

1. **Firmware installation** you perform deliberately.
2. **Deliberate settings or calibration changes**, written only when the value
   actually changes. Unchanged settings are never rewritten at boot, on
   reconnect or during recovery.

| Where | What | When |
| --- | --- | --- |
| Master NVS `pool_settings/doser_v2` (32 bytes, versioned, CRC) | Pump rating, calibration factor, single-dose limit, pre-run delay, minimum rest, maintenance intent | One save per actual change or explicit settings import; no background retry after a failure |
| Atlas NVS `atlas_intent_v1` (12 bytes) | Maintenance intent | Entering maintenance, or confirming probes are back in the pool |
| Atlas NVS `atlas_refs_v2` (44 bytes) | Nine calibration reference values | One save per changed value |
| NVS page headers (Master and Atlas) | Initializing blank or interrupted NVS | Only inside a deliberate save; never at boot and never by erasing the partition |
| EZO calibration memory | Calibration points or a confirmed clear | One command per armed, confirmed action; never replayed after a restart |
| EZO configuration (RTD scale and logging, EC outputs, K, TDS factor) | Only settings a query shows are wrong | Explicit configuration action; at most a few setters, none when already correct |
| Firmware partitions and OTA metadata | Application image and update state | Deliberate firmware installation only |
| HA `.storage/pool_tank` | Tank capacity and remaining ounces | Accepted dose-call response, or a capacity, level or refill change |
| HA `.storage/core.restore_state` | Auto toggle, with HA's other restored helpers | HA's normal state snapshots |

How this is enforced:

- `components/pool_storage/` is the only application settings writer. Loads
  open NVS read-only; saves compare existing bytes first and write nothing when
  they match.
- Linked partition wrappers reject every NVS write or erase outside a deliberate
  save, including framework and vendor paths. A build step adapts ESPHome's
  flash preferences backend so generic preference saves fail instead of writing,
  and it stops the build if the upstream file changes.
- Safe-mode counters use RTC memory, not flash. Wi-Fi driver NVS, stored PHY
  calibration and core dumps to flash are disabled. There are no restoring
  globals, numbers or switches.
- Dispatching an Atlas configuration setter invalidates its cached value, so an
  interrupted write is re-queried before any retry, and only remaining
  mismatches are written. Monitoring and reconnects send queries only.
- Blank or damaged NVS is left alone at boot; the next deliberate save
  initializes it. Initialization errors fail the save rather than erasing stored
  calibration.

`tests/test_flash_policy.py` exercises this policy with instrumented storage.
Host tests cannot measure physical flash wear, bootloader behavior or the EZO
chips themselves; those remain hardware checks.

## Tank estimate

The tank estimate lives in HA and is for display only:

- HA stores exactly two values: capacity and remaining ounces. Tank Level
  corrects the contents and Tank Refilled resets them to capacity.
- For each manual or automatic dose call, HA subtracts the full requested ounces
  only when that call's immediate response says the request was accepted. An
  accepted 10 oz request subtracts 10 oz even if it is interrupted. A rejected
  request, or a missing response, subtracts nothing.
- Status polls, completion, interruption, restarts and reconnects never change
  the estimate. There are no request IDs, duplicate markers, dose history,
  totals, catch-up or reconciliation.
- Zero or unknown contents never block dosing, and there is no cumulative dose
  limit in HA.

## Testing

Run both regression suites and build all three firmware targets after changes;
see the [README](../README.md#development-and-testing). The suites cover clock
loss and correction, stale and reordered messages, long outages, restarts,
lost replies, STOP, relay faults and the flash-write policy. They do not replace
water-only commissioning on real hardware. Never inject test pH values into live
MQTT topics.

References: [ESPHome SNTP](https://esphome.io/components/time/sntp/),
[ESPHome safe mode](https://esphome.io/components/safe_mode/),
[NIST Internet Time Service](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/internet-time-service-its),
[Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/storage/nvs_flash.html),
[Atlas EZO-RTD](https://files.atlas-scientific.com/EZO_RTD_Datasheet.pdf),
[Atlas EZO-EC](https://files.atlas-scientific.com/EC_EZO_Datasheet.pdf).
