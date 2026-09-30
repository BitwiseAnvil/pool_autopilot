# PoolDose wiring schedule — harness revision 14

**Ferrule-branch layout, using only the existing devices.**

> [!WARNING]
> **Known limitation: do not copy the neutral branches at Finder K5 terminal 3
> and K1 terminal 3.** Finder rates each 22.32 contact terminal for one 6 mm² (10 AWG) or two 4 mm² (12 AWG) conductors. Four 14 AWG wires at K5 terminal 3 (about 8.3 mm²) and three at K1 terminal 3 (about 6.3 mm²) exceed that rating.
> The reference enclosure is built this way. A new build should branch neutral
> on a rated DIN-rail neutral terminal block or other listed distribution so
> that each Finder terminal stays within its rating, and have the change
> checked by a qualified electrician.
[Color drawings](physical-wiring.html) · [PDF](pooldose-physical-wiring.pdf) ·
[Every wire end](termination-schedule.md) · [Assembly instructions](build-instructions.html).

After assembly, set the Mean Well MDR-20-5 supply to **5 V** and measure **5.00 V DC at
the supply output with all relays closed**. Then check communication, outlet
sensing, a normal timed run, STOP and flow-loss shutdown, and calibrate with
water as described in [pump calibration](pump-calibration.md).

Each shared endpoint is **one ferrule under one clamp**, containing the listed
14 AWG wires. FITTED marks an end as built in the reference enclosure; DATA
identifies specifications to record and CHECK identifies assembly checks.

## Confirmed limits and branch arrangement

Every 120 V wire is stranded **14 AWG**, including coil and detector leads.
Every small AC terminal receives one wire in one blue ferrule: Waveshare COM/NO,
Mean Well L/N/FG, detector A1/A2 and all Finder A1/A2 terminals. The 18 AWG wire
remains on the low-voltage/GPIO circuits. No 10 AWG wire is introduced.

| Branch point | Wires inside ONE ferrule | Outgoing connections |
|---|---|---|
| F1 top LOAD | A02 + A05 + A06 + A07 — four black 14 AWG | PS1 L, W2 COM, K1 terminal 1, W1 COM |
| K5 terminal 3 | A08 + A11 + A13 + A14 — four white 14 AWG | Incoming A08 from BOX N; branches to K1 terminal 3, PS1 N and K5 A2 |
| K1 terminal 3 | A11 + A15 + A16 — three white 14 AWG | Incoming A11 from K5 terminal 3; branches to K1 A2 and K4 A2 |
| K5 terminal 2 | B04 + B05 — two red 14 AWG | Pump-hot return and outlet detector |

**Ferrules:** three large red at F1 LOAD, K5 terminal 3 and K1 terminal 3;
one yellow at K5 terminal 2; the remaining 14 AWG device ends are blue. The
large red ferrules physically fit the Finder power terminals, but the K5-3 and
K1-3 groups exceed the terminal's rated capacity; see the warning above. There are no added Wagos or distribution devices outside the metal box.

**Finder terminal 3 is a power terminal, distinct from coil A1/A2.** Each
terminal 3 is used only as a physical neutral branch point. Terminal 4 has no
external wire; its 3–4 contact connects it to neutral when that Finder operates.
No required neutral path goes through a contact. Both coils therefore retain
neutral regardless of either contactor's state. The 1–2 poles remain the two
series pump-hot contacts. Never connect a neutral branch to terminal 1 or 2.

## Four wires at the metal box

The source-power and pump-outlet whips both terminate in the metal box. Each
has stranded 14 AWG **black hot, white neutral and green ground**. Their grounds
are joined on a ground Wago and bonded to the box; their neutrals join inside. The source
and outlet hot conductors stay on separate Wagos. Neutral and PE remain separate.

| Lead | Direction | Insulation / gauge | Connection |
|---|---|---|---|
| A01 | OUT | Black, 14 AWG stranded MTW, 600 V | Source-hot Wago → F1 bottom LINE |
| A08 | OUT | White, 14 AWG stranded MTW, 600 V | Neutral Wago → Finder K5 terminal 3 neutral branch |
| B04 | IN | Red, 14 AWG stranded MTW, 600 V | K5 terminal 2 → switched-return Wago → outlet-whip black |
| A17 | OUT | Green, 14 AWG stranded MTW, 600 V | Ground Wago → Mean Well PS1 FG (⏚) |

The feeder stays in the metal box. These are four separate control-harness
leads. No extra neutral lead or branch connection is added inside the box.
A17 is the only protective-earth conductor in the harness: Mean Well requires
the MDR-20 FG terminal to be connected to protective earth.
Follow the installed Wago strip/conductor instructions; no ferrules are specified
at the four box ends. Retain the existing protected entry and source protection.

F1 stays bottom-fed: **bottom LINE receives A01; top LOAD is the four-wire hot
branch.** Schneider permits [reverse feeding](https://www.se.com/us/en/faqs/FA114097/).
The [assembly instructions](build-instructions.html#assembly-details) list F1's
published terminal specifications and the ferrule barrel lengths.

## Shared ferrules and individual wire ends

| Terminal | All wire IDs in ONE ferrule | Each conductor | Termination note |
|---|---|---|---|
| F1:LOAD | A02 + A05 + A06 + A07 | Black / 14 AWG | FITTED: large red; tightened and connected as drawn |
| K5:3 | A08 + A11 + A13 + A14 | White / 14 AWG | FITTED: large red; tightened and connected as drawn |
| K1:3 | A11 + A15 + A16 | White / 14 AWG | FITTED: large red; fits Finder terminal 3 |
| K5:2 | B04 + B05 | Red / 14 AWG | FITTED: yellow around B04+B05; tightened |
| W2:VCC | C01 + C03 | Red / 18 AWG | DATA: record twin size/profile within the blue-sized envelope |
| W2:GND | C04 + C06 | Black / 18 AWG | DATA: record twin size/profile within the blue-sized envelope |

Six shared locations: two four-wire, one three-wire, one two-wire 14 AWG, and
two two-wire 18 AWG. All 17 single 14 AWG ends use the blue-single symbol;
all six single 18 AWG ends use the small-red-single symbol. Barrel length,
crimper range and each device's end note still apply. Collar color is separate
from wire insulation color. Crossings alone do not make connections.

The Pittsburgh 70195 ferrule kit chart lists metal-tube outside diameters: blue 14 AWG **2.6 mm**, yellow 10 AWG
**3.9 mm**, large red 8 AWG **4.9 mm**, and small red 18 AWG **1.7 mm**.
These are chart OD values, not specified dimensions after crimping or a
multiple-conductor selection table.

## Device order and terminal labels

**W1 Master / K2 → K1 Finder → W2 Slave / K3 → K5 Finder → K4 detector → PS1 Mean Well MDR-20-5 → F1 breaker**.
Waveshare bottom: NO, COM, B−, A+, left to right; top: GND left, VCC right.
J5 is the separate GPIO connector. Finder A1/A2 are the 120 V coil supply;
1–2 and 3–4 are distinct normally open contact pairs. K4 Phoenix 2966281:
input A1/A2, dry output 11/14, 12 unused. PS1 MDR-20-5: +V top left, −V top
middle, DC OK top right (unused); FG bottom left, N bottom middle and L bottom right. F1: LOAD top, LINE bottom.

B01 and B02 each supply one Finder A1 from its own Waveshare NO. A15 and A14
supply the respective A2 neutrals, each as a single blue end. Finder's LED and
mechanical indicator need no additional wiring. Finder's catalog covers stranded/ferruled
conductors. Its [installation sheet](https://cdn.findernet.com/app/uploads/IB2232EN.pdf)
has a separate solid-only note for field-wiring terminals; whether that note
applies to these internal chassis runs has not been established.

## Harness A — protected hot branches, neutral branches and supply ground

| Wire | From | To | AWG | Color | Function |
|---|---|---|---|---|---|
| A01 | BOX:L | F1:LINE | 14 AWG MTW, 600 V | Black | Separate stranded source-hot lead to bottom LINE |
| A02 | F1:LOAD | PS1:L | 14 AWG MTW, 600 V | Black | Protected-hot branch; single blue at Mean Well L |
| A05 | F1:LOAD | W2:COM | 14 AWG MTW, 600 V | Black | Protected-hot branch; single blue at Slave COM |
| A06 | F1:LOAD | K1:1 | 14 AWG MTW, 600 V | Black | Protected-hot branch to first pump-power contact |
| A07 | F1:LOAD | W1:COM | 14 AWG MTW, 600 V | Black | Protected-hot branch; single blue at Master COM |
| A08 | BOX:N | K5:3 | 14 AWG MTW, 600 V | White | Only white lead from box; enters four-wire neutral branch |
| A11 | K5:3 | K1:3 | 14 AWG MTW, 600 V | White | Neutral bridge between the two power-terminal branch points |
| A13 | K5:3 | PS1:N | 14 AWG MTW, 600 V | White | Neutral branch; single blue at Mean Well N |
| A14 | K5:3 | K5:A2 | 14 AWG MTW, 600 V | White | Slave coil neutral; single blue at A2 |
| A15 | K1:3 | K1:A2 | 14 AWG MTW, 600 V | White | Master coil neutral; single blue at A2 |
| A16 | K1:3 | K4:A2 | 14 AWG MTW, 600 V | White | Detector neutral; single blue at A2 |
| A17 | BOX:G | PS1:FG | 14 AWG MTW, 600 V | Green | Protective earth from the box ground Wago; single blue at Mean Well FG |

## Harness B — independent switching and pump return

| Wire | From | To | AWG | Color | Function |
|---|---|---|---|---|---|
| B01 | W1:NO | K1:A1 | 14 AWG MTW, 600 V | Red | Master relay switches K1 coil only; single blue at both ends |
| B02 | W2:NO | K5:A1 | 14 AWG MTW, 600 V | Red | Slave relay switches K5 coil only; single blue at both ends |
| B03 | K1:2 | K5:1 | 14 AWG MTW, 600 V | Red | Series motor-power link |
| B04 | K5:2 | BOX:SW | 14 AWG MTW, 600 V | Red | Single red return into the box to pump-outlet hot |
| B05 | K5:2 | K4:A1 | 14 AWG MTW, 600 V | Red | Final-output detector tap; single blue at detector A1 |

B04 and B05 share one installed yellow kit ferrule at K5 terminal 2. No loose Y splice or
extra box connection is added. The detector's A1/B05 and A2/A16 each receive
one 14 AWG wire. Neither controller's coil depends on the other Finder's state.

## Harness C — 5 V chains, RS485 and GPIO

The J5 plug colors are **1 yellow / GPIO1, 2 orange / GPIO2, 3 red / +3.3 V,
4 black / GND**. GPIO2 is unused on both boards.

**Master flow tail:** the W1 plug is soldered to telephone cord, with tape and heat-shrink insulation. The cord
connects to the RJ11-to-RJ11 coupling at the enclosure bottom; the generic
Hayward-style flow switch plugs into its other side. C09/C10 now identify the
installed cord's two used conductors: **yellow GPIO1 to red C09, and black
GND to green C10**. The finished tail is heat-shrunk. RJ11 labels below identify functions,
not numbered contacts; retain the installed contact mapping.

**Slave detector harness: orange and black, with the pull-up splice at the
plug leads.** Red 3.3 V joins directly to one end of R1.
At **JS2**, its other lead, yellow GPIO1 and orange C13 form one three-conductor
splice. **C13 alone goes to K4-14.** Black GND joins black C14 at JS3;
**C14 alone goes to K4-11.** The thin orange GPIO2 plug lead stays unused.
Heat-shrink each joint separately. This replaces the former
brown C11 extension and second orange C15 run; no twin is needed at K4-14.
[Sheet 07](pooldose-signals-wiring.svg) enlarges these connections;
[GPIO assembly steps](build-instructions.html#gpio-tails) explain each splice.

| Wire | From | To | AWG | Color | Function |
|---|---|---|---|---|---|
| C01 | PS1:+V | W2:VCC | 18 AWG MTW, 600 V | Red | 5 V supply to Slave; shares at Slave with onward C03 |
| C03 | W2:VCC | W1:VCC | 18 AWG MTW, 600 V | Red | Continue 5 V directly from Slave to Master |
| C04 | PS1:-V | W2:GND | 18 AWG MTW, 600 V | Black | DC return to Slave; shares at Slave with onward C06 |
| C06 | W2:GND | W1:GND | 18 AWG MTW, 600 V | Black | Continue DC return directly from Slave to Master |
| C07 | W1:A+ | W2:A+ | One conductor of intact Cat6A pair | Blue | RS485 A+; blue pair assigned for this build |
| C08 | W1:B- | W2:B- | Mate of same intact Cat6A pair | White/blue stripe | RS485 B−; preserve twist/jacket |
| C09 | JM1 | RJ11:FLOW | Installed telephone-cord conductor; gauge not recorded | Red | Completed: yellow Master GPIO1 to red phone-cord conductor and RJ11 flow-switch contact |
| C10 | JM2 | RJ11:RETURN | Installed telephone-cord conductor; gauge not recorded | Green | Completed: black Master GND to green phone-cord conductor and other RJ11 contact |
| C12 | R1:SENSE | JS2 | Integral R1 lead | Metal lead | R1 lead to the local GPIO1/orange-output splice; no added wire |
| C13 | JS2 | K4:14 | 18 AWG MTW, 600 V | Orange | One detector signal wire from the GPIO1/R1 splice to K4-14; single ferrule at K4 |
| C14 | JS3 | K4:11 | 18 AWG MTW, 600 V | Black | One detector return wire from black Slave GND to K4-11; single ferrule at K4 |

C07/C08 are two conductors of one intact Cat6A cable, not separate hookup-wire lengths.
C09/C10 are the installed telephone cord, not new cuts from the hookup-wire stock.
C12 is an integral R1 lead, not another cut wire. Retain the matching J5
pigtails' factory-crimped contacts. Individually insulate the red/R1 splice,
the three-conductor JS2 splice and the black return splice; support the
assembly as detailed in [assembly instructions](build-instructions.html).
Green C10 is a telephone-cord signal return, distinct from equipment ground.

## Counts and revision scope

**28 connection IDs / 56 ends.** 14 AWG ferrules: three large red
(two four-wire ends and one three-wire end), one yellow (the two-wire end),
and 17 single blue ends in the schedule. Harness C now has two shared
two-18-AWG ferrules at W2 VCC/GND and six single-18-AWG ends, including K4-11
and K4-14. There are **six shared device ferrules**. JS2 is a solder splice.

A03/A04/A09/A10/A12/C02/C05 remain retired. **C11/C15 are now retired**;
JS1/JS4 extension joints are superseded by the photographed JR1/JS2 layout.
Revision 14 replaces the Mean Well HDR-15-5 with an MDR-20-5 and adds green A17
from the box ground Wago to its FG terminal. Revision 13 recorded the
photographed GPIO harnesses; revision 12's 14 AWG routes, independent coil
control and series pump contacts are unchanged.
Use the current GPIO sheet and cut list together; firmware is unchanged.
