# Temperature (RTD) calibration

Open **Atlas Pool Kit → Visit → Calibration**.

1. Choose **RTD** and press **Start calibration**.
2. Put the probe in water with a known temperature. Enter that temperature in
   **°C**. Use **0 °C only for a properly prepared ice bath**.
3. Wait for the reading to settle. Press **Reading is steady**, then
   **Save calibration**. Wait for **Calibration saved**.
4. Put all probes back in pool water. Press **Exit calibration** and confirm.

To stop without saving, skip step 3 and exit. Simply starting RTD calibration
does not change its stored calibration.

## Preparing the reference

The kit's temperature probe is a standard PT1000, often installed in a
thermowell. The EZO-RTD supports single-point calibration;
it is not mandatory to alter its calibration merely to commission the other
features. [Atlas EZO-RTD datasheet](https://files.atlas-scientific.com/EZO_RTD_Datasheet.pdf)

A correctly prepared ice-point bath provides a reference near **0 °C / 32 °F**
without needing a second thermometer. A cup of cold water with a few ice cubes
is not equivalent. Use the preparation guidance and video in
[NIST's reference verification procedure](https://www.nist.gov/pml/sensor-science/thermodynamic-metrology/mercury-thermometer-alternatives/mercury-thermometer-7).
NIST's achievable laboratory uncertainty is not a guarantee for a homemade bath.

1. Check the actual probe's immersion requirements. Use clean crushed ice and
   clean water as directed by NIST, with ice distributed through the bath and
   excess water controlled. Ensure adequate insertion without touching the
   container's bottom or sides. Keep the electrical connector dry.
2. If verifying with the thermowell installed, ensure appropriate probe contact
   and allow the complete assembly to equilibrate. A thermowell adds response
   delay; it does not create a temperature reference.
3. Select **RTD - optional temperature calibration** and start. Enter **0 °C**
   only after establishing this bath. Observe the reading until stable.
4. **Verification alone is allowed:** record the result, end the session and
   return to monitoring without saving anything. If the reading is
   already acceptable for the application, leave the calibration unchanged.
5. To deliberately calibrate, press **Reading is steady → Save calibration**.
   Wait for **Calibration saved** after circuit acceptance and verification.
6. Return all probes, then **Exit calibration** and confirm. The normal TFT and MQTT
   temperature field remain Fahrenheit; this calibration reference is Celsius.

An ice-point check verifies one point, not accuracy throughout the pool's full
operating range. A trustworthy second reference near pool temperature can be
used later to assess that range. Do not assume room temperature is exactly
25 °C or that boiling water at your elevation is exactly 100 °C. If the reference cannot be
established, defer physical RTD calibration and record it as unverified.

The circuit command is `Cal,t` in Celsius, followed by `Cal,?`. The preview's
wider firmware range does not override the probe's physical temperature limits.
