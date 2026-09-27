# Gold ORP calibration

Use the kit's gold ORP probe and a **225 mV** calibration solution.
Follow [Atlas's EZO-ORP procedure](https://files.atlas-scientific.com/ORP_EZO_Datasheet.pdf)
and the probe care instructions for preparation and rinsing.

1. Open **Atlas Pool Kit → Visit → Calibration**, choose **ORP - 225 mV**,
   and press **Start calibration**. Verify the reference value.
   Other appropriate signed mV reference values can be entered deliberately.
2. Rinse and immerse the sensing end in the standard. Allow the preview to
   settle; a fixed wait alone does not demonstrate stability.
3. Press **Reading is steady → Save calibration** within ten seconds.
   Wait for **Calibration saved** after circuit acceptance and verification.
4. Return all probes to pool water, press **Exit calibration**, and confirm.
   Detailed results and calibration counts are available under **Advanced setup**.

This is one-point calibration (`Cal,225`, then `Cal,?`), not a pH-style series.
No pH/EC temperature compensation command is sent to ORP. Zero and negative ORP
remain valid measurements.

On interruption, inspect the circuit state and fresh preview before deliberately
re-arming. An unknown result does not mean the write failed. Accepted calibration
persists in the EZO circuit even if the firmware is rolled back.
