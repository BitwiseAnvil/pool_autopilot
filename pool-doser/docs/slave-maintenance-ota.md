# Slave Wi-Fi during maintenance

The Slave uses RS485 for every dosing command and status report. Its Wi-Fi
radio starts disabled and is enabled only by the Master's maintenance lockout
authorization. Wi-Fi serves encrypted ESPHome OTA; there is no Slave Home
Assistant API, MQTT, web server, captive portal, or fallback access point.

## Wi-Fi signal readings

The Master's Home Assistant device exposes two diagnostic sensors:

- **WiFi Signal**: the Master's signal strength in dBm, refreshed every 30 seconds.
- **Slave WiFi Signal**: the Slave's signal strength in dBm while Maintenance
  Lockout is ON and the Slave is connected to Wi-Fi, refreshed every 5 seconds.

The Slave reading becomes **Unknown** when its radio is off, it loses its Wi-Fi
connection, or RS485 replies stop. It does not retain an old reading as current.
The readout never enables the radio. Both readings appear on the existing Pool
Doser device; no separate Slave integration is required.

Protocol **v5** carries RSSI in status bits 9–15: magnitude 1–127 dBm,
zero unknown. The 45-byte frame also carries both controller sessions, the
command sequence and transaction. Controllers with mismatched protocol versions
discard each other's frames and cannot dose or authorize a maintenance radio
lease, so always install both from the same source revision.

## Operating behavior

| Condition | Behavior |
| --- | --- |
| Lockout OFF | Slave Wi-Fi is off; normal dosing interlocks still apply. |
| Lockout switched ON | Master opens Relay A and aborts Relay B. Shutdown verification completes before Wi-Fi is authorized. The Slave also blocks energizing commands locally. |
| Lockout ON, outputs safely off | The Slave enables Wi-Fi using the shared Doser network credentials. A live or unknown outlet detector inhibits Wi-Fi. |
| Lockout switched OFF | The switch stays ON until a fresh reply confirms the Slave has disabled Wi-Fi and opened its output. A missing reply keeps lockout ON. |
| OTA in progress | Both relays stay open. ESPHome blocks the Slave application loop during upload, so RS485 replies pause. An OFF request waits until the upload or error recovery finishes and the radio-off exchange succeeds. |
| Slave restarts | Relay B and Wi-Fi start off. A new Master request establishes the operating mode. |
| Master restarts | A restored OFF state also waits for a fresh Slave radio-off acknowledgment. A restored ON state renews maintenance authorization after outputs settle. Temporary startup lockout is RAM-only and never changes the saved operator choice. |
| RS485 authorization expires | After one second without a valid Master request, the Slave disables Wi-Fi and blocks dosing. An already-running upload can finish while the Master remains locked. |
| Upload fails | The Slave disables Wi-Fi, discards buffered RS485 input, and requires fresh authorization. If lockout remains requested ON, Wi-Fi returns for a retry. |

Authorization travels on every Master request, including the 250 ms heartbeat.
Replies report maintenance state, radio state, and readiness for normal control.
Lockout transitions use matching request sequences and a local generation counter
so a reply from an earlier transition cannot release a later lockout. Fault-clear
commands do not bypass maintenance, and enabling Wi-Fi does not clear physical faults. There is no device accounting.

The pending OFF request can be cancelled by switching lockout ON again. The
Slave may briefly have Wi-Fi off while the switch still shows ON during startup,
shutdown, reconnection, or the OFF acknowledgment exchange.

The Slave's hostname is **`pool-doser-slave`**, normally resolved locally as
**`pool-doser-slave.local`** while Wi-Fi is enabled. A quick physical check:
it answers ping with lockout ON and stops answering with lockout OFF.

### Operator intent across power interruption

The Master's settings record stores the deliberately requested maintenance
state separately from the effective lockout it holds while waiting for a fresh
safe radio-off acknowledgment. Repeated ordinary power interruptions therefore
never save a temporary startup lockout as the operator's choice. Regression
tests exercise this separation.

## First installation

Both controllers are first flashed over USB. Install the Slave and Master from
the same source revision so their protocol versions match. The Slave boots
with Wi-Fi off and waits for its Master; nothing else is needed on the Slave
side. Build commands never upload firmware.

## Subsequent Wi-Fi updates

From `pool-doser/`, with the ESPHome environment available:

```sh
esphome compile pool-doser-slave.yaml
# Enable Maintenance Lockout and wait for the Slave to join Wi-Fi first.
esphome upload pool-doser-slave.yaml --device pool-doser-slave.local
```

Use the Slave's DHCP address in place of the hostname if local mDNS resolution
is unavailable. The CLI reads its encrypted OTA key from `secrets.yaml`. Wait
for the upload, reboot, and RS485 reconnection before releasing lockout. When
a protocol change affects both controllers, update the Slave first under its
lease, then the Master.

Both configurations share the adjacent ignored `secrets.yaml`, created from
`secrets.example.yaml`. The Master uses `api_encryption_key` for both its API
and its required encrypted OTA. The Slave uses its own random 32-byte Base64
`slave_ota_key` (for example `openssl rand -base64 32`); keep it locally and
reuse it for every later upload. Do not regenerate keys for an ordinary update.

USB remains the recovery path when the Slave cannot execute RS485 authorization,
when the RS485 link cannot be restored, or when its OTA key is lost. ESPHome safe
mode does not run the normal UART/control components, and Wi-Fi remains disabled
there. Enabling a recovery radio without lockout would break this design's rule.

## Software verification and physical checks

`../tools/test-doser` includes sanitizer-backed simulations using the actual
Master/Slave YAML lambdas. These cover normal dosing, active-dose lockout,
shutdown still pending, local rejection of energizing commands during
maintenance, fault-clear isolation, ON/OFF transitions, duplicate requests,
lost requests/replies, stale acknowledgments, controller restarts, OTA success
and error recovery, unknown/live outlet sensing, old protocol/CRC rejection,
and timer rollover. Both firmware configurations must also validate and compile.

Simulation cannot verify RF startup, network reachability, actual flash writes,
or physical relay timing. Check these on the built hardware: Slave reachability
with lockout ON and OFF, relay state during an upload, and behavior when RS485
authorization is deliberately interrupted.
