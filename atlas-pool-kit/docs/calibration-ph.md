# Lab-grade pH calibration

Use the kit's lab-grade pH probe and fresh pH 7.00, 4.00 and 10.00 buffers.
Read [Atlas's EZO-pH procedure](https://files.atlas-scientific.com/pH_EZO_Datasheet.pdf)
and the probe's storage/rinsing instructions. Do not dry the pH probe as part of
EC dry calibration. Keep unused buffer uncontaminated; do not return used
solution to its original container.

1. Open **Atlas Pool Kit → Visit → Calibration** and choose **pH - 3 points**. One point and either
   two-point combination are also available. Three-point order is 7 → 4 → 10.
2. Establish the buffer temperature. Prefer placing the RTD in the same buffer,
   allowing it to settle, and enabling **Temperature probe** before
   beginning. Otherwise enter an actually measured buffer temperature. The
   initial 25 °C reference setting is not evidence that the buffer is at 25 °C.
   Use the buffer's temperature/reference information when entering pH values.
3. Select **Start calibration**. All six normal pool readings become unavailable.
   Rinse as instructed and immerse the probe in the midpoint buffer. If using
   RTD compensation, move the RTD with the pH probe for each reference.
4. Watch the current reading until stable. Press **Reading is steady**, then **Save calibration**
   within ten seconds. Reapplying midpoint removes the circuit's other points.
5. Wait for the next step. Rinse between references and repeat for the indicated
   low and high steps. Wait for **Calibration saved** after the last point.
6. Return every probe to pool water, press **Exit calibration**, and confirm.
   Detailed results and calibration counts are available under **Advanced setup**.

The firmware uses `Cal,mid,n`, `Cal,low,n`, `Cal,high,n` and `Cal,?`; the operator
does not need to type them. Temperature-compensated calibration is enabled only
for pH circuit revision 2.13 or later. Older/unknown revisions require an
explicit compatibility review, not a blind write.

If interrupted, use the recovery workflow in the [README](../README.md#4-calibration). A failed
verification can follow an accepted write; cancelling cannot restore the
previous calibration. Never use **Clear calibration** as a routine first step.
