# Harness cutting and measurement worksheet — revision 13

Use this worksheet for measurement records. Blank length/continuity fields are not test results.

After assembly, measure 5.00 V at the DC supply output with all relays closed, then confirm Home Assistant/RS485 communication, outlet sensing, a normal timed run, STOP and flow-loss shutdown. Calibrate with water before chemical service; see pump-calibration.md. Completed wiring does not replace these measurements.

Measure each route on the de-energized assembly. Reference crimper: Sopoby HSC8 6-4; record the actual tool range for each end.
Inventory: stranded hookup wire in 10 ft lengths per color; A01 uses stranded 14 AWG black.
C07/C08 are one intact cable length; C12 is an existing resistor lead. Every intended shared group is listed in the termination schedule.

C09/C10 are installed red/green telephone-cord conductors. C13/C14 are orange/black detector wires. C11/C15 are retired.

| Wire | Color / gauge | From → To | Measured cut length | Ends labeled | Continuity |
|---|---|---|---|---|---|
| A01 | Black / 14 AWG MTW, 600 V | BOX:L → F1:LINE | ____ | ☐ | ☐ |
| A02 | Black / 14 AWG MTW, 600 V | F1:LOAD → PS1:L | ____ | ☐ | ☐ |
| A05 | Black / 14 AWG MTW, 600 V | F1:LOAD → W2:COM | ____ | ☐ | ☐ |
| A06 | Black / 14 AWG MTW, 600 V | F1:LOAD → K1:1 | ____ | ☐ | ☐ |
| A07 | Black / 14 AWG MTW, 600 V | F1:LOAD → W1:COM | ____ | ☐ | ☐ |
| A08 | White / 14 AWG MTW, 600 V | BOX:N → K5:3 | ____ | ☐ | ☐ |
| A11 | White / 14 AWG MTW, 600 V | K5:3 → K1:3 | ____ | ☐ | ☐ |
| A13 | White / 14 AWG MTW, 600 V | K5:3 → PS1:N | ____ | ☐ | ☐ |
| A14 | White / 14 AWG MTW, 600 V | K5:3 → K5:A2 | ____ | ☐ | ☐ |
| A15 | White / 14 AWG MTW, 600 V | K1:3 → K1:A2 | ____ | ☐ | ☐ |
| A16 | White / 14 AWG MTW, 600 V | K1:3 → K4:A2 | ____ | ☐ | ☐ |
| B01 | Red / 14 AWG MTW, 600 V | W1:NO → K1:A1 | ____ | ☐ | ☐ |
| B02 | Red / 14 AWG MTW, 600 V | W2:NO → K5:A1 | ____ | ☐ | ☐ |
| B03 | Red / 14 AWG MTW, 600 V | K1:2 → K5:1 | ____ | ☐ | ☐ |
| B04 | Red / 14 AWG MTW, 600 V | K5:2 → BOX:SW | ____ | ☐ | ☐ |
| B05 | Red / 14 AWG MTW, 600 V | K5:2 → K4:A1 | ____ | ☐ | ☐ |
| C01 | Red / 18 AWG MTW, 600 V | PS1:+V → W2:VCC | ____ | ☐ | ☐ |
| C03 | Red / 18 AWG MTW, 600 V | W2:VCC → W1:VCC | ____ | ☐ | ☐ |
| C04 | Black / 18 AWG MTW, 600 V | PS1:-V → W2:GND | ____ | ☐ | ☐ |
| C06 | Black / 18 AWG MTW, 600 V | W2:GND → W1:GND | ____ | ☐ | ☐ |
| C07 | Blue / One conductor of intact Cat6A pair | W1:A+ → W2:A+ | ____ | ☐ | ☐ |
| C08 | White/blue stripe / Mate of same intact Cat6A pair | W1:B- → W2:B- | ____ | ☐ | ☐ |
| C09 | Red / Installed telephone-cord conductor; gauge not recorded | JM1 → RJ11:FLOW | Installed telephone cord | ☐ | ☐ |
| C10 | Green / Installed telephone-cord conductor; gauge not recorded | JM2 → RJ11:RETURN | Installed telephone cord | ☐ | ☐ |
| C12 | Metal lead / Integral R1 lead | R1:SENSE → JS2 | Integral lead | ☐ | ☐ |
| C13 | Orange / 18 AWG MTW, 600 V | JS2 → K4:14 | length ____ | ☐ | ☐ |
| C14 | Black / 18 AWG MTW, 600 V | JS3 → K4:11 | length ____ | ☐ | ☐ |

Before crimping, total the measured lengths per gauge/color and compare with stock. Include service loops and termination allowance.
All onward harness links are real scheduled wires at device screws. The two whips and their Wago connections stay at the metal box.
Record end-preparation and continuity measurements here. Follow the [assembly instructions](build-instructions.html) for checkout and [pump calibration](pump-calibration.md) for the water test.
