# Automatic acid control

HA requests correction above **7.80** using the Master's fresh, time-weighted
**two-minute pH window**. HA requests one **2 oz** pulse at a time. At or below
7.80, no new pulse is requested; an already accepted pulse can finish under local
protection. Atlas remains a sensor monitor and never commands the doser.

The shared runtime-state, flash-write and time rules are described in
[design and safety](../../docs/design-and-safety.md).

## Operator intent and recovery

**Auto Enabled** is `input_boolean.doser_auto_enabled` in HA, displayed as an
on/off toggle. With no `initial` override, HA restores both On and Off across
restarts. A new installation starts Off. The doser has no Auto mode to save or
restore; it checks each bounded request independently. Requests, observations,
timers and faults stay in RAM. Device maintenance is a deliberate saved setting.

**STOP** is `script.doser_stop`: turn the HA toggle Off, then immediately abort
the dose. The abort opens outputs before any other device work and saves nothing.
Turning Auto Off only prevents new automatic requests: a dose already running,
automatic or manual, finishes under its normal bounds and local protection. If
communication is unavailable, Off remains selected in HA. Flow loss, communication
pauses and startup lockout never change the HA choice.

| Condition | Behavior |
| --- | --- |
| Master restart | Outputs off; discard old request, token, result and rest countdown; restore maintenance and rebuild fresh checks; HA retains Auto intent |
| HA restart | Restore the toggle On or Off; create a new observer session and rebuild the fresh pH window before any new automatic request |
| Slave restart or RS485 loss | Terminate the affected delivery, invalidate energizing commands and rediscover sessions; recover after fresh status/proving |
| Master power loss opens A before Slave timeout | Slave treats lost output as an interruption; this alone does not create a permanent relay latch |
| HA/API/Wi-Fi/MQTT interruption | An accepted bounded pulse may finish locally; new requests wait for fresh observations after reconnection |
| No flow, fresh readings, usable calibration or clock | Wait until the actual prerequisite recovers |
| Confirmed relay isolation/shutdown or contradictory feedback fault | Latch in RAM; reconnect does not clear it; deliberate maintenance recovery or a full power cycle followed by fresh proving is required |
| Deliberate STOP/Auto Off/maintenance | Preserve the selected intent through restarts |
| Unknown/empty tank or HA tank-storage failure | Display-only; dosing interlocks are unaffected |

Startup requires five-minute continuous flow qualification and five-minute
mixing, concurrently with the two-minute pH window. There is no extra restart
hold. Subsequent doses require mixing plus the configured minimum rest; setting
rest to zero does not remove mixing. HA downtime does not stop local elapsed
waits. Periodic status queries recover when waits have expired; no timer event
or missed-opportunity queue is needed.

All requests use the same single-dose bound, optional pre-run delay, flow
interlock, A/B relay proving, verified shutdown and independent 15-minute runtime
ceiling. Confirmed physical faults are distinct from interrupted communication.
No tank balance, cumulative dose limit, allowance or accounting history is needed.

## Freshness

The HA validator accepts only fresh Monitoring diagnostics with
`diagnostic_version: 2`, `configuration_ok: true`, no deliberate maintenance or
calibration recovery, and a healthy NIST clock. Samples carry a distinct Atlas
boot/clock epoch and frozen 64-bit Unix acquisition time. Samples must be under
20 seconds old; diagnostics/publication under 10 seconds, with at most two seconds
of clock lead. Duplicates, retained and reordered messages cannot renew freshness.

Atlas and Master synchronize only with `time.nist.gov`, at startup/reconnect and
every five minutes. Clock health expires after 30 minutes without sync; a clock
step clears the old observation window. The Master independently validates sample
time and expires HA's heartbeat after 15 seconds. Startup or reconnect creates a
new HA observer session and requires a new pH window. See
[design and safety](../../docs/design-and-safety.md).

## Public actions and response version 3

The Master exposes these Home Assistant actions:

- `esphome.pool_doser_dose(ounces)` requests a manual bounded dose.
- `esphome.pool_doser_acid_status(client_session)` returns current status and,
  only when ready, a one-use token expiring after ten seconds. Another query
  replaces an unused token.
- `esphome.pool_doser_automatic_dose(request_id)` consumes that token, rechecks
  readiness and requests exactly 2 oz. Empty, stale, consumed and previous-boot
  tokens cannot authorize output. A refusal preserves the latest accepted result.
- `esphome.pool_doser_acid_observe_ph(valid, boot, sample_id, ph)` forwards an
  observation and cannot start a dose. Invalid data clears the window.
- `esphome.pool_doser_acid_pause` clears the observation window and unused token,
  preserving any accepted bounded request. It does not change HA's Auto choice.
- `esphome.pool_doser_import_operating_settings(...)` creates the complete
  settings record during first configuration; see
  [pump calibration](pump-calibration.md#first-configuration).

`esphome.pool_doser_stop` immediately aborts the current dose and clears old
observation/token state. Use the HA STOP script for operator intent as well as
abort. The firmware has no Auto switch or Stop button; the HA toggle and STOP
script appear on the Pool dashboard. The firmware status entity is
**Request Readiness** (`sensor.pool_doser_request_readiness`). It
reports safety readiness independently of HA's On/Off choice and pH threshold.

Status, manual and automatic responses carry `response_version: 3`, `boot_id`,
`accepted_sequence`, active/latest request identity, result (`none`, `accepted`,
`completed`, `interrupted`), result reason and requested ounces. Readiness, pause reason,
request reply/token, pH average, flow/mixing/rest waits,
controller/physical-fault state and NIST clock health remain available. There are
no Auto/correction state, accounted ounces, rolling totals, device tank fields or runtime-journal health.

An accepted request exists only in RAM. After a lost API reply, later status
queries refresh control readiness; they never replay the dose or update the tank.
A controller restart changes its boot identity and starts accepted
sequence at zero. RS485 v5 independently binds commands to current Master/Slave
sessions and a staged transaction. Unsupported response/protocol versions fail
closed for automatic requests and energizing commands respectively.

## HA tank estimate

For each manual or automatic dose call, subtract the full requested ounces
only when its immediate response confirms acceptance. Accepted 10 oz means
subtract 10 oz, even if interrupted. Rejected requests and missing acceptance
responses subtract nothing.

Tank Capacity, Tank Level, Tank Refilled and Tank Estimate appear on the Pool dashboard.
Level correction and refill set contents directly. Save only capacity and remaining
ounces as tank data. Status polling, completion, interruption, restart and reconnect
never change the estimate. There are no tank request IDs, boot IDs, sequence
markers, duplicate tracking, catch-up accounting, dose history or lifetime totals.
Zero or unknown contents never block dosing.

The package passes each immediate dose-call response to
`pool_tank.record_acceptance` in its `response` field. The component requires
`response_version: 3` and `request_reply: accepted` before deducting `requested_oz`.
The `result` field describes the latest accepted request and can still say
`accepted` on a busy rejection. For example, a missed 10 oz acceptance followed
by a rejected call subtracts nothing, even if that rejection reports the old
10 oz request. A later status query cannot recover the missed deduction.

Status polls never invoke tank reporting. The component stores only `capacity_oz` and `remaining_oz` in
HA's Store data, saving when either value changes. It keeps no request tracking
and provides no replay or reconciliation action. Use Tank Level or Tank Refilled
for a deliberate correction. Install the component and package together as
described in the [Home Assistant setup guide](../../home-assistant/README.md).

## Verification

`./tools/test-doser --container` runs protocol, actual-YAML scheduler, maintenance,
HA action/schema and real HA helper restart/Store/entity tests in a throwaway
Home Assistant container. Coverage
includes immediate accepted-response deductions, rejected/missing replies,
no deductions from status polls, two-value tank storage, unchanged storage across
HA restart, interrupted pulses, refill ordering, stale sessions and acknowledgments,
Master power-loss timing, peer restarts, physical faults, STOP, no settings retries,
60-day simulated monitoring, timer rollover and 30/50-day sample outages.
Framework/vendor write instrumentation runs in `./tools/test-atlas --container`.
All three firmware configurations must also validate and build using the pinned
toolchain. Software checks do not verify physical flash or chemical delivery.
