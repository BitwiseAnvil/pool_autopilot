# Rebuilding the wiring book

Harness revision 13 documents ferrule branches at the existing devices. All
120 V wires are stranded 14 AWG, and each small AC terminal receives one wire
in one blue ferrule. The reference build uses large red ferrules at F1 LOAD,
K1-3 and K5-3, yellow at K5-2 and blue at the remaining 14 AWG device ends;
`FITTED` marks those as-built ends. The K5-3 and K1-3 groups exceed Finder's
rated terminal capacity; `TERMINAL_WARNING` in `wiring_data.py` carries the
known-limitation warning into every generated output.

The eight SVG sheets, the combined `pooldose-wiring-diagram.svg`, the offline
viewer, the JSON connection list, the both-end Markdown table and the bench
cut/check sheet are generated together:

```sh
python3 docs/tools/build_physical_wiring.py
python3 tests/test_wiring_documentation.py
```

There are **27 A/B/C wire IDs / 54 ends**. Only A01, A08 and B04 cross the metal-box
boundary. BOX endpoints represent existing in-box Wagos, not added external
distribution stops. The graph checks model both Finder contact pairs in all
four switching states, proving continuous neutral, independent coil control
and two series pump-hot contacts. Drawing checks verify endpoints, insulation
colors and paths that avoid unrelated terminals.

## Sources

- `docs/wiring-schedule.md` owns IDs, endpoints, gauges and insulation colors.
- `wiring_data.py` adds physical clamp descriptions, `SHARED_ENDS` groups with
  two, three or four wires, `SHARED_CHECKS` notes, `BUILD_CHECKS` and the
  reference-build `ASSEMBLY_RECORD`. `wiring-hardware.json` exports these; it
  contains no added devices or hidden links. All onward connections are real
  wire rows.
- `build_physical_wiring.py` draws the device order from the enclosure photo
  on the rail sheets. Sheet 07 uses a separate enlarged layout for the Slave
  J5 plug, R1 and K4 contacts, with the Master flow-switch tail below.
- `wiring-viewer.html` is the viewer template. Edit it, not the generated
  `physical-wiring.html`.

The J5 factory leads are yellow GPIO1, unused orange GPIO2, red 3.3 V and
black GND. C09/C10 are red/green telephone-cord conductors to the RJ11
coupling at the enclosure bottom; RJ11 contact labels are functional, not
numbered pin assignments. The Slave uses a local three-conductor JS2 splice
joining yellow GPIO1, R1 lead C12 and orange C13; black C14 is the return.
Both K4 signal terminals take single wires. C11/C15 are retired. `M2_14`
records two 14 AWG wires in one yellow kit ferrule; `M3_14` and `M4_14` use
large red.

The reference photo is `docs/images/enclosure-2026-09-18.jpg`. The tools use
it locally; they do not upload it. Routes are not measured cut lengths.
Neutral branches are at K5-3 and K1-3, with single blue ends at both Finder
A2s, PS1 N and K4 A2. K4 A2 receives A16; B05 is single at K4 A1. Both Finder
terminal 4s are externally unwired, but become neutral when their respective
3–4 contact closes. The shared-ferrule sheet details all six groups.

## PDF export

To refresh the PDF, install Playwright in a documentation-only virtual
environment outside the repository and provide a Chromium executable (or
install Playwright's Chromium):

```sh
python3 -m venv /tmp/pooldose-docs-venv
/tmp/pooldose-docs-venv/bin/pip install playwright
/tmp/pooldose-docs-venv/bin/playwright install chromium
/tmp/pooldose-docs-venv/bin/python docs/tools/export_physical_wiring.py --screenshots /tmp/pooldose-wiring-review
```

For an existing browser add `--browser /absolute/path/to/chrome`. The exporter
checks navigation, both-end highlighting, keyboard cycling through every wire
in two-/three-/four-wire shared groups, JS2 selection, single small-terminal
ends, visible dimension/specification notes, filtering, phone overflow and complete print
output. It writes `docs/pooldose-physical-wiring.pdf` as A3 landscape with
selectable vector/text content. Printing from the viewer produces all eight
sheets plus the complete termination table, regardless of screen filtering.

Review screenshots and PDF pages after drawing changes. Documentation checks
verify schedule consistency, circuit connectivity and viewer behavior. Firmware
does not depend on these rendering packages.
