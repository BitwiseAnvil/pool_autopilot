# Parts list

Everything on the reference equipment pad besides the pool pump, filter and
plumbing. Prices change often, so check each link at checkout. The doser's
electronics and wiring have their own detailed
[Pool Doser BOM](pool-doser/BOM.md).

## Water sensing

| Qty | Part | Purpose | Supplier |
| ---: | --- | --- | --- |
| 1 | Atlas Scientific Wi-Fi Pool Kit | ESP32-S3 TFT with EZO-pH, EZO-ORP and EZO-RTD circuits, probes and calibration solutions | [Atlas Scientific](https://atlas-scientific.com/kits/wi-fi-pool-kit/) |
| 1 | Atlas Scientific EZO-EC conductivity circuit | Adds conductivity, salinity and TDS on the kit's AUX port | [Atlas Scientific](https://atlas-scientific.com/embedded-solutions/ezo-conductivity-circuit/) |
| 1 | Atlas Scientific conductivity probe K 1.0 (ENV-40-EC-K1.0) | Conductivity probe for the EZO-EC | [Atlas Scientific](https://atlas-scientific.com/probes/conductivity-probe-k-1-0/) |
| 2 packs | POBADY SMA male to SMA female bulkhead cable, 15 cm RG316, 2 per pack | Waterproof probe pass-throughs in the enclosure floor | [Amazon](https://www.amazon.com/dp/B09N3LR587) |

## Acid dosing

| Qty | Part | Purpose | Supplier |
| ---: | --- | --- | --- |
| 1 | Stenner 15-gallon chemical tank with 45MPHP10 fixed pump (S1G45MFH2A1SUAA), 10 GPD at 100 PSI, 120 V | Acid tank and peristaltic pump the doser switches | [PoolWeb](https://www.poolweb.com/products/15-gallon-gray-chemical-tank-with-45mphp10-model-fixed-pump-100-psi-10-gpd-120-volt-1-4-inch-standard-tubing) |
| 1 | Doser controllers, contactors, detector, supply and breaker | Two-controller fail-off pump switching | [Pool Doser BOM](pool-doser/BOM.md) |
| 1 | Eaton TR5260WS-SP-L surge-protective receptacle, 15 A 125 V, tamper-resistant, LED | Pump outlet (J1) switched by the doser | [Amazon](https://www.amazon.com/dp/B0GG8KCQ2V) |
| 1 | Normally-open paddle flow switch with 2-inch tee | Proves water is flowing before and during every dose | [Amazon](https://www.amazon.com/dp/B0D52PPW6R) |
| 1 | QIANRENON RJ11 6P4C bulkhead coupler, IP68 | Waterproof flow-switch pass-through in the enclosure floor | [Amazon](https://www.amazon.com/dp/B0GK6FMQLM) |

## Salt chlorination

The salt cell runs on its own controller. This project does not control it,
but salt chlorination steadily pushes pH up, which is what the acid doser
corrects.

| Qty | Part | Purpose | Supplier |
| ---: | --- | --- | --- |
| 1 | CircuPool RJ-60 Plus salt chlorine generator, up to 60,000 gallons | Makes chlorine from salt in the pool water | [Discount Salt Pool](https://www.discountsaltpool.com/circupool-rj-60-plus-salt-chlorine-generator) |
| 1 | EverCrystal sacrificial zinc anode with 3.2 ft wire | Corrosion protection for metal in a salt pool | [Amazon](https://www.amazon.com/dp/B0F6MZ87LY) |

## Enclosure and power

| Qty | Part | Purpose | Supplier |
| ---: | --- | --- | --- |
| 1 | IP67 ABS enclosure, 410 × 310 × 180 mm, hinged cover, stainless latches, mounting plate and wall brackets | Houses the doser and the Atlas kit together | [Amazon](https://www.amazon.com/dp/B0DZ6JLK6F) |
| 1 box | WAGO 221-615 Lever-Nuts, 5-conductor, 20–10 AWG, box of 15 | Splices inside the metal power-entry box | [Amazon](https://www.amazon.com/dp/B07W4MGWPS) |

Mains wiring must follow local codes and should be done or inspected by a
qualified electrician. See [Safety](README.md#safety).
