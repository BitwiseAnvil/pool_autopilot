# Pump calibration

Keep **Pump Rating** at the pump's labeled gallons per day; for a 10 GPD
Stenner, enter **10**. Use the separate **Calibration Factor** box on the Pool
Doser device's Configuration panel to correct the amount delivered.
**Maintenance Lockout** belongs in the separate Controls panel.

The factor multiplies run time:

- **1.000** uses the rated output without correction.
- Below **1.000** shortens the run when the pump delivers too much.
- Above **1.000** lengthens the run when the pump delivers too little.

The field accepts **0.010–10.000** in **0.001** increments and is saved on the
controller only when deliberately changed. Its entity ID is
`number.pool_doser_calibration_factor`. Add the devices before assigning any
area: Home Assistant otherwise adds the area name to new entity IDs.

## First configuration

A new Master has no saved settings record. It shows firmware defaults, reports
`configuration_required` and refuses to dose. Editing a single number does not
save those defaults as real settings. Create the record once, with
**Maintenance Lockout** on and no dose running, by calling the response-only
action `esphome.pool_doser_import_operating_settings` (Developer Tools →
Actions, with the response enabled) and all five values:

```yaml
action: esphome.pool_doser_import_operating_settings
data:
  pump_rating: 10          # labeled GPD
  calibration: 1.0         # start at 1.000, then calibrate below
  single_dose_limit: 13    # US fl oz
  pre_run_seconds: 60
  minimum_rest_seconds: 1800
```

The response reports `import_result: imported` and echoes the saved values. It
refuses with `refused_busy_or_not_in_maintenance` or `refused_invalid_settings`
and saves nothing. After the record exists, edit the individual number
entities normally; each change saves only when the value changes. A corrupt or
missing record blocks dosing again until it is recreated this way; the
controller never substitutes defaults.

## Measure and calculate

1. Use water with the tubing primed. Collect the outlet delivery in a graduated
   container under conditions representative of the intended installation.
2. Request a known amount within the runtime and ounce limits. At **10 GPD**
   and factor **1.000**, a **10 US fl oz** request runs the outlet for about
   **11 minutes 15 seconds**, plus the separate pre-run delay and relay checks.
3. Let that request finish normally and measure the actual collected volume.
   Measure the collected water directly. Use the same volume units for requested
   and collected amounts;
   one US fl oz is approximately **29.5735 mL**.
4. Calculate and enter:

   `new factor = current factor × requested volume ÷ collected volume`

5. Repeat a measured request to verify the resulting amount. Keep Pump Rating
   at the labeled value.

For example, starting at factor **1.000**, requesting **10 oz** and collecting
**12 oz** gives `1 × 10 / 12 = 0.833`. Collecting **8 oz** instead gives
`1 × 10 / 8 = 1.250`. Repeated adjustments multiply the current factor by the
new requested/collected ratio; they do not always start again from one.

The requested/collected formula assumes a completed dose. For an interrupted
run, use measured volume and actual pump-on time instead:

`factor = (Pump Rating ÷ 11.25) × pump-on minutes ÷ collected US fl oz`

Neither formula is defined for zero collected volume. Establish water delivery
before attempting a volume calibration.

## Weighing water

A scale is easier than a graduated container. Weigh the water alone,
excluding the container. Near **75 °F**, 10 US fl oz of water weighs
approximately **295 g**, using the
[NIST water-density formulation](https://pmc.ncbi.nlm.nih.gov/articles/PMC4909168/).
For example, collecting **330 g** at factor **1.000** gives
`1.000 × 295 / 330 = 0.894`; if the repeat collects **293 g**, the next factor
is `0.894 × 295 / 293 = 0.900`. At 10 GPD and factor **0.900**, a 10 oz request
runs the pump for about **607.5 seconds** (10 minutes 8 seconds), plus the
separate startup delay and relay checks.

## What changes in the controller

`run time (minutes) = requested US fl oz × 11.25 ÷ Pump Rating × factor`

`Estimated Pump Flow Rate (US fl oz/minute) = Pump Rating ÷ (11.25 × factor)`

The controller switches the pump on and off; it does not control pump speed.
With **10 GPD** and factor **0.900**, the estimate rises from
**0.889** to **0.988 oz/min** because the pump delivers more than its labeled
rating predicts. It therefore needs less time to deliver the requested
amount: a 10 oz run changes from **11:15** to approximately **10:08**.

`sensor.pool_doser_output_rate` (**Output Rate** on the device page) is the
estimated pump flow rate in oz/min.

The corrected rate sets pulse runtime. HA's display-only tank estimate subtracts
the full requested ounces only from the dose call's immediate accepted response,
regardless of interruption. Editing the
factor does not change the tank estimate. Changes are accepted while the dose
sequence is idle, so a running dose uses the runtime calculated when it started.

The independent **15-minute maximum per run** applies to the corrected duration.
Increasing Max Single Dose does not extend that ceiling. There is no cumulative
24-hour dose limit or device accounting.
