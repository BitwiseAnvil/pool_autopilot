# Pool Math readings and CSI

The [package](packages/pool_math.yaml) adds five test readings from the Pool Math
app and a live CSI to the **Water** section of the
[Pool dashboard](README.md). Set `pool_math_url` in `secrets.yaml` to your share
link's JSON form, for example `https://api.poolmathapp.com/share/<id>.json`.

One [REST request](https://www.home-assistant.io/integrations/rest/) polls that
link every 600 seconds. Only `pools[0].pool.overview` supplies data:

| Reading | Value / original timestamp | Entity |
| --- | --- | --- |
| Free Chlorine | `fc` / `fcTs` | `sensor.pool_math_free_chlorine` |
| Total Alkalinity | `ta` / `taTs` | `sensor.pool_math_total_alkalinity` |
| Stabilizer / CYA | `cya` / `cyaTs` | `sensor.pool_math_cya` |
| Calcium Hardness | `ch` / `chTs` | `sensor.pool_math_calcium_hardness` |
| Borates | `bor` / `borTs` | `sensor.pool_math_borates` |

Each tile shows a value such as `12 ppm — 9 days ago`. The sensor's state is the
numeric ppm value; its `reading` attribute holds the tile text and `tested_at`
the original test timestamp. Age is whole 24-hour periods since that test and
refreshes every minute.

## Last values are kept

Each reading keeps its last valid value and test time until Pool Math returns a
newer valid one. A restart, network outage, rate-limited request (HTTP 429) or
missing field never blanks the tiles; the age keeps counting from the original
test. These are trigger-based template sensors, which Home Assistant restores
across restarts. A reading shows Unknown only before its first successful fetch.

Pool Math limits how often a share link can be read. Avoid restarting Home
Assistant repeatedly or forcing extra updates; the 10-minute poll is enough.

## CSI

`sensor.pool_csi` uses the [TFP PoolMath CSI equation](https://www.troublefreepool.com/calc.html)
with its pH-dependent CYA and borate alkalinity corrections and ionic-strength
salt correction. Total alkalinity, calcium hardness, CYA and borates come from
the kept readings above; pH, water temperature and salinity come live from
`sensor.atlas_pool_ph`, `sensor.atlas_pool_temperature` (°F or °C) and
`sensor.atlas_pool_salinity`. CSI is rounded to two decimal places and is
unavailable only when an input is missing or outside the equation's range.

The package does not read `recentLogs` or Pool Math pH, temperature, salt or
CSI. These entities are excluded from the recorder. It contains no dosing
actions.

## Verification

From the repository root, using the Python that has ESPHome installed (see the
[repository README](../README.md#development-and-testing)):

```sh
"${POOL_ESPHOME_PYTHON:-python3}" tests/test_pool_math.py
```

The check runs in a throwaway Docker container from
`ghcr.io/home-assistant/home-assistant:stable`, or from the image named by
`POOL_HA_IMAGE`; no running Home Assistant is touched. It covers overview-only
extraction, original timestamps, ages, keeping the last values through
unavailable, rate-limited and incomplete responses, a newer test replacing a
kept value, five outputs from TFP's published JavaScript calculator, °F/°C
equivalence and missing CSI inputs.
