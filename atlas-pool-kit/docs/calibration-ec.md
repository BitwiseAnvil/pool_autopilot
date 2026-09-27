# K1.0 conductivity calibration

This guide assumes a **K1.0 probe** on an EZO-EC circuit and Atlas's K1.0
calibration standards of **12,880 and 80,000 µS/cm**. This one procedure
calibrates conductivity and the circuit's derived salinity/TDS outputs.
Follow [Atlas's EZO-EC instructions](https://files.atlas-scientific.com/EC_EZO_Datasheet.pdf).

1. Wait for a valid **EC Probe K** readback. If it differs from 1.0, end any
   calibration session, confirm the installed probe is a K1.0 model,
   and use the advanced
   **Configure installed K1.0 probe** action. Wait for verified readback.
2. Open **Atlas Pool Kit → Visit → Calibration** and select **EC - dry, low, high**.
   Use low 12,880 and high 80,000 µS/cm when those steps appear.
   Dry plus one wet point is available as a separate method. These references
   are conductivity values, not salt ppm or TDS ppm.
3. Bring the standards near their stated 25 °C reference temperature. Firmware
   suspends normal RTD compensation and applies 25 °C for EC calibration. If
   using a different actual temperature, use the standard's documented reference
   chart; do not guess a correction. An unverified RTD is not a precision
   thermometer solely because it displays many digits.
4. Press **Start calibration**. Clean and thoroughly dry the **EC** probe as Atlas directs.
   Leave it in air. Press **Reading is steady → Save calibration**; its reference is zero.
   Wait for the low step. Never dry the pH/ORP probes for this step.
5. Immerse in the low standard with the sensing area covered and bubbles removed.
   Enter the low reference and allow readings to settle. Press
   **Reading is steady → Save calibration**. Wait for the high step. As documented on
   [datasheet page 70](https://files.atlas-scientific.com/EC_EZO_Datasheet.pdf#page=70),
   low acceptance leaves readings unchanged until the high point is applied.
   A reading that still differs from the 12,880 reference is therefore expected
   at this stage; it is not a failed agreement check or a completed calibration.
6. Rinse between standards following Atlas's instructions, then repeat for high.
   Wait for **Calibration saved** at the high point. This checks the two-point
   status and the high-reference reading after Atlas applies the calibration.
   Avoid carry-over, keep unused standards clean, and follow pouch storage/use
   instructions after opening.
7. Return every probe to pool water. Press **Exit calibration** and confirm.
   Detailed results and calibration counts are under **Advanced setup**. Normal measured-RTD compensation is
   restored before normal EC, salinity and TDS readings become available.

Commands are `Cal,dry`, `Cal,low,n`, `Cal,high,n` (or `Cal,n` for a single wet
point) and `Cal,?`. The circuit's calibration count reports wet points; successful
dry calibration is recorded separately in session progress. An intermediate
count after low does not establish completion. Ending the session after low
leaves the high point and final verification incomplete.

The TDS conversion factor is separate configuration. Its stored value is shown
and preserved; an explicit advanced action can set 0.01…1.00 and verify readback.
Changing it must affect TDS without applying another scale to conductivity or
salinity. Default 0.54 is not a claim that it best describes your pool's water.
The device's Salt row, MQTT salinity values and Home Assistant salinity entity
report Atlas salinity multiplied by 1,000 in ppm. The display and Home Assistant
show whole ppm. TDS remains a separate reading with its own conversion factor.

K and TDS-factor actions require a valid current readback and skip matching
values. If a configuration write is interrupted, wait for the circuit to be
queried again before confirming another attempt. The old value is no longer
trusted; recovery queries never replay the setter. This rule also applies to
EC output changes through **Configure monitoring settings**. Calibration points
still require their separate reference and verification procedure.

A separate USB carrier for the EZO-EC is only a bench tool. The firmware
expects the EZO-EC in the kit's AUX I²C connection at address 100; it does not
communicate with an external USB carrier. Verify that arrangement during
commissioning. Do not change interface mode or address automatically.
