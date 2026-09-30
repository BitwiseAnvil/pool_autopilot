# Pool Doser BOM

Harness revision 14: two `ESP32-S3-Relay-1CH-U` controllers, two independent
Finder contactors, an outlet-live detector, a DIN-rail 5 V supply and a branch
breaker, wired with ferrule branches at the existing device terminals. The
[color wiring book](docs/physical-wiring.html) follows the mounted order:
W1 Master, K1 Finder, W2 Slave, K5 Finder, K4 Phoenix, PS1 Mean Well, F1 Schneider.
Coil varistors are not pump-motor suppression.

> [!WARNING]
> The reference build's neutral branches at Finder K5 terminal 3 and K1
> terminal 3 exceed the terminal rating. Finder rates each 22.32 contact terminal for one 6 mm² (10 AWG) or two 4 mm² (12 AWG) conductors. Four 14 AWG wires at K5 terminal 3 (about 8.3 mm²) and three at K1 terminal 3 (about 6.3 mm²) exceed that rating.
> Do not copy them; add a rated DIN-rail neutral terminal block. See the
> [wiring schedule](docs/wiring-schedule.md).

The reference pump is a 120 V Stenner Classic 45MPHP10: 1.7 A, 1/30 HP,
10 GPD at 100 PSI. Recheck the contactor selection for any other pump and
measure starting current before service.

## Parts

Prices are what the reference build paid, before tax and shipping; verify
them at checkout.

| # | Qty | Part | Purpose | Supplier | Reference price |
|---:|---:|---|---|---|---:|
| 1 | 2 | ESP32-S3-Relay-1CH-U, SKU 35086 | External-antenna controller, isolated RS485, DIN case | [Waveshare](https://www.waveshare.com/esp32-s3-relay-1ch.htm?sku=35086) | $38.78 for 2 |
| 2 | 2 | Finder 22.32.0.120.1320 | K1 and K5: each 17.5 mm, 120 V varistor-protected coil; independent coils, NO 1–2 power contacts in series | [Amazon](https://www.amazon.com/dp/B00NMQFT6Y) | $48.75 each |
| 3 | 1 | Phoenix Contact 2966281 | 120 VAC outlet-live detector with hard-gold contact | [Mouser 651-2966281](https://www.mouser.com/en/ProductDetail/Phoenix-Contact/2966281?qs=wd%252Bw3mUqFrlInk3ycQdn9Q%3D%3D) | $25.07 |
| 4 | 1 | Mean Well MDR-20-5 | DIN supply, 120 VAC to 5 VDC/3 A, 22.5 mm wide; input from the doser's common protected branch/disconnect; FG terminal to protective earth. Also powers the Atlas kit | [Mouser 709-MDR20-5](https://www.mouser.com/ProductDetail/MEAN-WELL/MDR-20-5?qs=TaOZSEYtRiVHaoS93zq5aQ%3D%3D) | $14.40 |
| 5 | 1 | CF14JT270R | 270 Ω Slave outlet-detector pull-up | [Mouser 708-CF14JT270R](https://www.mouser.com/ProductDetail/SEI-Stackpole/CF14JT270R?qs=FESYatJ8odLq71IBt5AZfw%3D%3D) | $0.10 |
| 6 | 1 | Normally-open paddle flow switch, for example Watflow B0D52PPW6R GLX-FLO-style kit | Switch, 15 ft cable and 2-inch PVC tee; verify normally-open operation before installation | [Amazon](https://www.amazon.com/dp/B0D52PPW6R) | $16.23 |
| 7 | 1 | F1: Schneider Electric 60106 Multi 9 C60, one-pole, 5 A C-curve DIN-rail circuit breaker | UL 489 listed, 120 VAC, 10 kA interrupting rating; local overcurrent protection of the protected hot | [Schneider Electric](https://www.se.com/us/en/product/60106/multi9-c60-ul489-mcb-1-pole-5-a-c-curve-120-v-10-ka-tunnel-term/) | $12.00 |
| 8 | 10 lengths, 10 ft each | CrimpZone UL1015/MTW 600 V hookup wire: 14 AWG black, red, white, dark green; 18 AWG black, red, yellow, orange, dark blue, brown | Enclosure power, control and signal colors documented below | [14 AWG](https://www.crimpzone.com/14-mtw-hook-up-wire-pick-color-length/) and [18 AWG](https://www.crimpzone.com/18-mtw-hook-up-wire-pick-color-length/) | $41.12 |

**Approximate total: $245.20** at these reference prices. The Amazon prices
are volatile. The Watflow assembly includes its 2-inch tee.

For a one-shot Mouser order, paste this into Mouser's [Price and Availability
Assistant](https://www.mouser.com/en/price-availability/) as
**Mouser # | Qty**:

```text
651-2966281 | 1
709-MDR20-5 | 1
708-CF14JT270R | 1
```

## Flow switch

Wire the normally-open switch contact directly between Master GPIO1 and GND;
the GPIO's internal pull-up senses the closure at very low current. Before
installation, use a meter to verify that the switch is open at rest and closes
only when its paddle moves; return it if it does not.

The reference build solders the W1 GPIO plug to telephone cord, insulated with
heat-shrink, ending at an RJ11-to-RJ11 coupling at the enclosure bottom. A
generic Hayward-style flow switch plugs into the other side.

## Power entry

F1 is the Schneider 60106 UL 489 branch-circuit breaker, rated 5 A.
The source-power and pump-outlet whips each have three stranded 14 AWG conductors
(black hot, white neutral, green ground) and terminate in the metal box.
**A01 is a separate stranded 14 AWG black harness lead
from the in-box hot Wago to F1 bottom LINE**; **A02 is 14 AWG black
from F1 top LOAD**. This bottom-fed arrangement is permitted by Schneider's
[C60 reverse-feed guidance](https://www.se.com/us/en/faqs/FA114097/).
The other box-boundary leads are **A08 white 14 AWG out**, **B04 red 14 AWG
back in to the outlet-whip black hot** and **A17 green 14 AWG out to the Mean
Well FG terminal**. Both whip neutrals join inside the box.
Both whip grounds and A17 are joined on a ground Wago and bonded to the box. The source-whip black hot
and outlet-whip black hot connect to separate Wagos.

Retain upstream protection appropriate to the source whip; F1 protects its
downstream circuit. Follow the breaker's terminal markings and
[C60 terminal guidance](https://www.se.com/us/en/faqs/FA89727/).

## Fit

The two controllers, two contactors, outlet detector, and power supply occupy **110.3
mm (4.34 in)** of DIN rail before adding F1. The Schneider breaker adds 18 mm
(0.71 in), for **128.3 mm (5.05 in)** total before end stops and wiring
clearance. PS1 requires 5 mm free on each side, so reserve at least **138.3 mm**
before end stops and routing allowance; specify a **200 mm rail**. Mount PS1
upright with AC at the bottom and leave **40 mm above / 20 mm below**
(150 mm vertical envelope around its 90 mm case). Keep this space free of
wire bundles, duct, end stops and enclosure walls. Measure the clearance from
K4 on the left and F1 on the right of PS1. See
[assembly details](docs/build-instructions.html). The plumbing flow-switch kit
is not a DIN device.

## Power supply

PS1 powers both doser controllers and the Atlas kit. The reference build
first used a Mean Well HDR-15-5. With it, the Atlas pH reading swung about
0.07 pH from minimum to maximum on average. On 2026-09-30 it was replaced by
the Mean Well MDR-20-5 listed above, and the swing dropped to about a quarter
of that, roughly 0.025 pH. Use the MDR-20-5; do not substitute the HDR-15-5.

Per the Mean Well [MDR-20 datasheet](https://www.meanwell.com/Upload/PDF/MDR-20/MDR-20-SPEC.PDF)
and [installation manual](https://www.meanwell.com/Upload/PDF/MDR%20DIN%20rail.pdf):
85–264 VAC input, 5 V at 3 A (15 W), 80 mVp-p maximum ripple, 22.5 × 90 ×
100 mm. Terminals: +V, −V and DC OK on top (DC OK unused); FG, N and L on the
bottom. The FG terminal must be connected to protective earth; green A17 does
this. Use copper wire rated at least 80 °C, strip 6.5 mm and tighten to
5.0 lb-in (0.57 N·m). Mount it upright with the AC terminals at the bottom.

## Also required

- Pump and chemical tank. The HA tank estimate defaults capacity to 1,920 oz
  (15 gallons) and leaves contents unknown until a baseline, level correction
  or refill.
- Pump receptacle J1 and upstream GFCI branch protection. The reference build
  uses an [Eaton TR5260WS-SP-L](https://www.amazon.com/dp/B0GG8KCQ2V) tamper-resistant
  surge-protective receptacle with an indicator LED. Do not add a second
  GFCI in series, and keep the GFCI upstream of the sensed output.
- Cat6A: use its intact blue pair for RS485, blue A+ and white/blue B−;
  do not pull a bare pair from the jacket. Check actual AWG and solid/stranded
  construction; do not ferrule solid conductors.
- Wire ferrules. The reference build uses the
  [Pittsburgh 70195 1,200-piece assortment](https://www.harborfreight.com/wire-ferrule-assortment-1200-piece-70195.html).
  Use ferrules only where the screw-clamp terminal manufacturer permits them.
  No ferrules inside crimp splices or unapproved box/receptacle terminations.
- A ferrule crimper. The reference build uses a
  [Sopoby HSC8 6-4](https://www.amazon.com/dp/B06XCZC89W): a self-adjusting
  ratchet with a four-sided square crimp. Its listing's description gives
  0.25–6 mm² (23–10 AWG) and its headline bullets 0.08–10 mm² (28–7 AWG);
  both include the 18/14 AWG single-ferrule sizes. Record your tool's actual
  range alongside the selected shared ferrules.
- Wagos inside the metal box for the four harness connections, following
  their conductor range and strip length.

Use 14 AWG wire from item 8 on the F1-protected 120 V circuits:
black/red/white/dark green for hot/switched hot/neutral/equipment ground.
Grounding connections stay inside the metal box except green A17, which runs
from the ground Wago to the Mean Well FG terminal. Use the 18 AWG assortment only on isolated 5 V/3.3 V
wiring: red/black for +5 V/0 V and **orange/black for the detector
signal/return**. The Master flow tail uses the telephone cord's red/green pair.
The GPIO harnesses do not need the brown, dark-blue or yellow 18 AWG stock.
There is no 18 AWG white.

Not priced because they depend on the final layout: enclosure/subpanel, DIN
rail and mounts, chemical containment/ventilation, tubing, valves, and
injection hardware. Both listed Finders have varistor-protected coils. No
motor suppressor is specified; J1 is the surge-protective receptacle listed
above. Finder's
[installation instructions](https://cdn.findernet.com/app/uploads/IB2232EN.pdf)
confirm A1/A2 operating power and the contact layout, and its catalog covers
stranded/ferruled conductors. The installation sheet's separate solid-only
note is labeled for field-wiring terminals; its applicability to this internal
chassis harness has not been established and is not treated as a blanket
stranded-wire prohibition. Use the [bench cut list](docs/harness-cut-list.md)
to measure the revision-14 A/B/C routes and total each color against the
10 ft lengths. Lay out the harness on a de-energized bench, not in the final
120 V installation.

## Existing-hardware harness scope

Wagos remain inside the metal box for the four harness connections. The
[wiring book](docs/physical-wiring.html) shows ferrule branches at F1 LOAD and
Finder K1/K5 power terminal 3.

A01 is a separate stranded 14 AWG black lead from the in-box hot Wago to F1
bottom LINE. A08 is the only white neutral lead out, to K5 terminal 3; B04 is
the red switched return. A17 is the green 14 AWG ground lead from the ground
Wago to the Mean Well FG terminal. Measure the routes and service loops against the
10 ft of each color. Keep the 128.3 mm device footprint and PS1 clearances
specified above; no additional distribution assembly is needed.

## Ferrule sizes and shared ends

The Pittsburgh 70195 kit chart lists these relevant single-wire sizes:

| Collar | Label | Barrel length | Metal-tube OD in kit chart |
|---|---|---|---|
| Small red | 18 AWG | 8 mm | 1.7 mm |
| Blue | 14 AWG | 8 mm | 2.6 mm |
| Yellow | 10 AWG | 12 mm | 3.9 mm |
| Large red | 8 AWG | 12 mm | 4.9 mm |

OD is the tube's outside diameter, not a finished crimp dimension. Ferrule
labels do not change the hookup-wire sizes: every 120 V wire remains 14 AWG.
The assortment does not provide a multiwire combination table.

| Treatment | Quantity | Locations |
|---|---|---|
| Single 14 AWG / blue | 17 | Remaining 14 AWG device ends, including all Finder A1/A2 and PS1 FG |
| Single 18 AWG / small red | 6 | Four 5 V/return ends, plus orange C13 at K4-14 and black C14 at K4-11 |
| Large red / four 14 AWG conductors | 2 | F1 LOAD and K5 terminal 3 |
| Large red / three 14 AWG conductors | 1 | K1 terminal 3 |
| Yellow / two 14 AWG conductors | 1 | K5 terminal 2, B04+B05 |
| One twin / two 18 AWG conductors | 2 | W2 VCC/GND only; both K4 signal terminals receive single wires |

No branching is placed at Mean Well L/N/FG, Waveshare COM/NO, Finder A1/A2 or
detector A1/A2. The low-voltage twins must fit the blue-sized envelope.

The [end list](docs/termination-schedule.md) identifies every wire and shared
group. Both three- and four-wire 14 AWG symbols are red; the two-wire K5-2
symbol is yellow. Low-voltage twin symbols remain gray.
Ferrule collar color is independent of insulation color. Follow the actual
ferrule/terminal instructions; do not trim strands or force a crimp into a clamp.
Solid Cat6A stays bare. The four box ends follow the Wago instructions;
no ferrule is specified there.

## Harness C GPIO materials

Use the supplied factory-crimped four-position SH1.0 pigtails, R1 and the
Master telephone-cord/RJ11 assembly. The Slave detector harness uses orange
C13 and black C14 from the 18 AWG stock. Its local JS2 splice joins three
conductors: yellow GPIO1, the resistor lead and orange C13. Solder,
heat-shrink and supports are not priced above.

Factory plug colors: yellow = pin 1 / GPIO1; orange = pin 2 / GPIO2 (unused);
red = pin 3 / 3.3 V; black = pin 4 / GND. Red 3.3 V joins R1 directly; its
other lead joins the yellow/orange node locally. Orange C13 alone reaches
K4-14; black C14 alone reaches K4-11. On the Master, yellow GPIO1 joins the
red phone wire and black GND the green phone wire. See
[the GPIO steps](docs/build-instructions.html#gpio-tails).

[Assembly instructions](docs/build-instructions.html) describe the GPIO joints,
including the three-conductor JS2 splice, single K4 ends and unplugged-harness
measurements. The Waveshare mains leads are A05/A07/B01/B02, each single
14 AWG at the board. Record terminal data and ferrule selection in the end
schedule; check the existing exposed-metal bond during assembly checkout.
