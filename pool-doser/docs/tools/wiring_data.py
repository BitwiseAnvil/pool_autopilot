"""Reviewed device-chain connections with separate terminal preparation notes."""
from pathlib import Path
import re

DOCS = Path(__file__).resolve().parents[1]
HARNESSES = {'A': 'AC supply', 'B': 'Switching', 'C': 'Low voltage'}
CONNECTION_REVIEW = 'Revision 14 electrical graph checks pass: independent coil control, continuous neutral, two series pump contacts, the local GPIO1 pull-up splice and a separate PS1 FG ground lead.'
BUILD_CHECKS = ('After assembly, measure 5.00 V at the DC supply output with all relays closed, then confirm Home Assistant/RS485 communication, outlet sensing, a normal timed run, STOP and flow-loss shutdown. '
                'Calibrate with water before chemical service; see pump-calibration.md. Completed wiring does not replace these measurements.')
TERMINAL_WARNING = ('Known limitation: do not copy the neutral branches at Finder K5 terminal 3 and K1 terminal 3. '
                    'Finder rates each 22.32 contact terminal for one 6 mm² (10 AWG) or two 4 mm² (12 AWG) conductors. '
                    'Four 14 AWG wires at K5-3 (about 8.3 mm²) and three at K1-3 (about 6.3 mm²) exceed that rating. '
                    'The reference enclosure is built this way. A new build should branch neutral on a rated DIN-rail neutral terminal block '
                    'or other listed distribution so that each Finder terminal stays within its rating, and have the change checked by a qualified electrician.')
DESIGN_STATUS = ('Reference build: all three harnesses assembled with the ferrules below and the Mean Well MDR-20-5 supply set to 5 V. '
                 + BUILD_CHECKS)
ASSEMBLY_RECORD = {
    'basis': 'Reference build',
    'completed_harnesses': ['A', 'B', 'C'],
    'power_up': {'supply': 'Mean Well MDR-20-5', 'output_v': 5.0,
                 'loaded_output_check': {'measured_v': 5.0, 'condition': 'All relays closed', 'measurement': 'DC supply output'}},
    'build_checks': BUILD_CHECKS,
    'shared_ferrules': {'F1:LOAD': 'Large red', 'K5:3': 'Large red', 'K1:3': 'Large red', 'K5:2': 'Yellow'},
    'known_limitation': TERMINAL_WARNING,
    'remaining_14_awg_device_ferrules': 'Blue',
    'gpio_plug_colors': {'1': 'Yellow / GPIO1', '2': 'Orange / GPIO2', '3': 'Red / 3.3 V', '4': 'Black / GND'},
    'master_flow_tail': 'Yellow GPIO1 spliced to RED telephone-cord C09 and black GND to GREEN telephone-cord C10, finished in heat-shrink. An RJ11 coupling at the enclosure bottom connects to a generic Hayward-style flow switch. Cord gauge and numbered contact mapping are not specified.',
    'slave_gpio': 'RED 3.3 V factory lead joined directly to R1; its other lead joins YELLOW GPIO1 and ORANGE C13 at one three-conductor splice; BLACK GND joins BLACK C14. Insulate each joint separately.',
}
# Wagos stay inside the metal box, represented by BOX endpoints. Supply branches
# use existing device screws; GPIO branches use the photographed solder joints.
STRIPS = {}
INTERNAL_LINKS = []
SHARED_ENDS = {
    'F1:LOAD': ('A02','A05','A06','A07'),
    'K5:3': ('A08','A11','A13','A14'), 'K1:3': ('A11','A15','A16'),
    'K5:2': ('B04','B05'), 'W2:VCC': ('C01','C03'), 'W2:GND': ('C04','C06'),
}
SHARED_CHECKS = {
    'F1:LOAD': 'FITTED: one LARGE RED ferrule holding A02/A05/A06/A07, four black 14 AWG wires, tightened and connected as drawn.',
    'K5:3': 'FITTED: one LARGE RED ferrule holding A08/A11/A13/A14, four white 14 AWG wires, tightened. Reference build only: exceeds the Finder terminal rating; see the known-limitation warning.',
    'K1:3': 'FITTED: one LARGE RED ferrule holding A11/A15/A16, three white 14 AWG wires, tightened. Reference build only: exceeds the Finder terminal rating; see the known-limitation warning.',
    'K5:2': 'FITTED: one YELLOW ferrule holding B04/B05, two red 14 AWG wires, tightened and connected as drawn.',
    'W2:VCC': 'DATA: record the two-18-AWG twin size/profile within the blue-sized envelope and the Waveshare clamp strip/torque specification.',
    'W2:GND': 'DATA: record the two-18-AWG twin size/profile within the blue-sized envelope and the Waveshare clamp strip/torque specification.',
}
DESCRIPTIONS = {
    'BOX:L': 'Inside metal box: hot Wago joins source-whip black to separate black A01 lead OUT',
    'BOX:N': 'Inside metal box: neutral Wago joins source-whip neutral, outlet-whip neutral and white A08 lead OUT',
    'BOX:SW': 'Inside metal box: switched-hot Wago joins red B04 lead IN to outlet-whip black',
    'BOX:G': 'Inside metal box: ground Wago joins both whip grounds, the box bond and green A17 lead OUT',
    'F1:LINE': 'F1 bottom LINE clamp; A01 incoming supply', 'F1:LOAD': 'F1 top LOAD clamp; one installed large-red ferrule with four black 14 AWG branches',
    'PS1:+V': 'PS1 top LEFT +V', 'PS1:-V': 'PS1 top MIDDLE −V',
    'PS1:FG': 'PS1 bottom LEFT FG (⏚)', 'PS1:N': 'PS1 bottom MIDDLE N', 'PS1:L': 'PS1 bottom RIGHT L',
    'K4:A1': 'K4 top outer A1 input clamp', 'K4:A2': 'K4 top inner A2 input clamp',
    'K4:11': 'K4 bottom inner 11 common', 'K4:14': 'K4 bottom middle 14 normally open',
    'JM1': 'JM1: Master J5-1 GPIO1 YELLOW factory lead to RED telephone-cord conductor C09; photographed splice',
    'JM2': 'JM2: Master J5-4 GND BLACK factory lead to GREEN telephone-cord conductor C10; photographed splice',
    'RJ11:FLOW': 'Installed RJ11 coupling at enclosure bottom: GPIO1 contact of the connected flow-switch pair; functional label, not a contact number',
    'RJ11:RETURN': 'Installed RJ11 coupling at enclosure bottom: GND contact of the connected flow-switch pair; functional label, not a contact number',
    'JR1': 'JR1: Slave J5-3 RED 3V3 factory lead joined directly to one R1 lead; no brown extension',
    'R1:SENSE': 'Integral R1 sense-side lead; C12 is not cut hookup wire',
    'JS2': 'JS2: ONE local three-conductor splice joins YELLOW GPIO1 factory lead, R1 lead C12 and ORANGE C13 to detector terminal 14',
    'JS3': 'JS3: Slave J5-4 GND BLACK factory lead joined to BLACK C14 for detector terminal 11',
}
for device, role in [('W1','Master / K2'),('W2','Slave / K3')]:
    for pin, position in [('NO','bottom 1st / LEFTMOST'),('COM','bottom 2nd'),
                          ('B-','bottom 3rd'),('A+','bottom 4th / RIGHTMOST'),
                          ('GND','top power LEFT'),('VCC','top power RIGHT (+5 V)')]:
        DESCRIPTIONS[f'{device}:{pin}'] = f'{device} {role}: {position} screw {pin}'
for device in ['K1','K5']:
    for pin, position in [('1','top outer LEFT'),('2','bottom outer LEFT'),('3','top outer RIGHT / neutral branch'),
                          ('A1','upper inner ledge'),('A2','lower inner ledge')]:
        DESCRIPTIONS[f'{device}:{pin}'] = f'{device} Finder: {position} screw {pin}'

PREPARATION = {
    'M4_14': 'ONE LARGE RED ferrule containing FOUR 14 AWG conductors, Pittsburgh 70195 kit chart: 8 AWG label, 12 mm barrel, 4.9 mm tube OD.',
    'M3_14': 'ONE LARGE RED ferrule containing THREE 14 AWG conductors, at K1 terminal 3. Pittsburgh 70195 kit chart: 8 AWG label, 12 mm barrel, 4.9 mm tube OD.',
    'M2_14': 'ONE YELLOW ferrule containing TWO 14 AWG conductors, at K5 terminal 2. Pittsburgh 70195 kit chart: 10 AWG label, 12 mm barrel, 3.9 mm tube OD. This records the kit ferrule used, not a separate twin-ferrule product.',
    'T18': 'ONE two-wire ferrule containing TWO 18 AWG conductors. Record its size, barrel length and crimp profile. Check retention of each wire and metal-barrel engagement at the clamp.',
    'S14': 'SINGLE 14 AWG ferrule. Kit: blue collar, 8 mm barrel; kit chart metal-tube OD 2.6 mm. Check device-specific note.',
    'S18': 'SINGLE 18 AWG ferrule. Kit reference: SMALL red collar, 8 mm barrel; kit chart metal-tube OD 1.7 mm. Check device-specific note.',
    'BOX': 'Wago connection inside the metal box; follow the installed connector conductor range and strip length. No ferrule specified at this end.',
    'DATA': 'Identify Cat6A gauge/construction and clamp specification. Solid: bare. Stranded: only an accepted matching ferrule.',
    'J': 'Mechanically engaged solder joint, individually sleeved and strain-relieved; no ferrule.',
    'RJ11': 'Existing telephone-cord RJ11 contact. Retain the installed contact mapping; this drawing assigns no numbered RJ11 pinout.',
    'R': 'Integral resistor lead C12; sleeve the exposed metal and its splice. No extra hookup wire or ferrule at this end.',
}
PREP_LABEL = {'S14':'SINGLE 14', 'S18':'SINGLE 18', 'M4_14':'4 × 14 / ONE', 'M3_14':'3 × 14 / ONE',
              'M2_14':'2 × 14 / ONE', 'T18':'SHARED 18', 'BOX':'BOX',
              'DATA':'CABLE END', 'J':'SOLDER', 'RJ11':'RJ11 CONTACT', 'R':'R1 LEAD'}
COLOR = {'Black':'#202832','Red':'#d93136','White':'#ffffff','Yellow':'#f4c627',
         'Green':'#26834b','Orange':'#ed7c19','Metal lead':'#a5abb2',
         'Blue':'#217dcb','White/blue stripe':'#ffffff'}

def preparation(endpoint, wire):
    if endpoint in SHARED_ENDS:
        size=len(SHARED_ENDS[endpoint])
        return 'T18' if wire.startswith('C') else f'M{size}_14'
    if endpoint.startswith('BOX:'): return 'BOX'
    if endpoint.startswith('RJ11:'): return 'RJ11'
    if endpoint == 'R1:SENSE': return 'R'
    if endpoint.startswith('J'): return 'J'
    if wire in {'C07','C08'}: return 'DATA'
    return 'S18' if wire.startswith('C') else 'S14'

def preparation_note(endpoint, code):
    device = endpoint.split(':')[0]
    if endpoint == 'JS2':
        return 'Three-conductor solder splice: YELLOW GPIO1 factory lead + R1 lead C12 + ORANGE C13. Individually insulate this node from the RED 3V3 splice and support the resistor assembly.'
    if endpoint in SHARED_ENDS:
        note = SHARED_CHECKS[endpoint] + ' Wires: ' + ' + '.join(SHARED_ENDS[endpoint]) + '. ' + PREPARATION[code]
        return note
    if device in {'K1','K5'}:
        limit = ' A1/A2: one 14 AWG wire in one blue ferrule; no branch at this coil terminal.' if endpoint.endswith((':A1', ':A2')) else ''
        return 'Single blue 14 AWG ferrule. Catalog strip 9 mm, torque 0.8 N·m; kit barrel 8 mm.' + limit
    if device == 'PS1':
        return ('Single blue 14 AWG ferrule. ' if code=='S14' else 'Single 18 AWG ferrule. ') + 'The design limits all Mean Well terminals to a blue-single-14-AWG ferrule envelope; confirm the selected ferrule/barrel. MDR manual: copper, at least 80°C insulation, 6.5 mm strip, 5.0 lb-in (0.57 N·m); kit barrel is 8 mm.'
    if device == 'F1':
        return PREPARATION[code] + ' C60 wire guidance: 14 mm strip and 22 lb-in for this current/voltage range, subject to installed markings. Kit blue barrel is 8 mm; check the metal-barrel engagement against the device instructions.'
    if device in {'W1','W2'}:
        return PREPARATION[code] + ' DATA: record the installed Waveshare clamp wire/ferrule range, strip and torque.'
    if device == 'K4':
        return PREPARATION[code] + ' K4 supports single ferrules 0.2–2.5 mm²; strip 8 mm, torque 0.6–0.8 N·m.'
    return PREPARATION[code]

def load_wires():
    result = []
    for line in (DOCS/'wiring-schedule.md').read_text().splitlines():
        if not re.match(r'\| [ABC]\d{2} \|',line): continue
        fields = [s.strip() for s in line.strip('|').split('|')]
        if len(fields) != 6: continue  # Summary/boundary tables are not wire rows.
        row = dict(zip(('id','from','to','gauge','color','function'),fields))
        row['ends'] = [row['from'], row['to']]
        row['physical'] = [DESCRIPTIONS[e] for e in row['ends']]
        row['prep'] = [preparation(e,row['id']) for e in row['ends']]
        row['prep_notes'] = [preparation_note(e,p) for e,p in zip(row['ends'],row['prep'])]
        row['hex'] = COLOR[row['color']]
        row['harness'] = row['id'][0]
        row['sheet'] = ('neutral' if row['harness']=='A' and row['color']=='White' else
                        {'A':'mains','B':'switched'}.get(row['harness'], 'dc' if int(row['id'][1:])<=3 else 'returns' if int(row['id'][1:])<=8 else 'signals'))
        row['prep_labels'] = [PREP_LABEL[p] for p in row['prep']]
        row['end_status'] = ['FITTED' if row['harness'] in 'AB' or row['id'] in {'C09','C10','C12','C13','C14'} else
                             SHARED_CHECKS[e].split(':')[0] if e in SHARED_CHECKS else
                             'DATA' if e.startswith(('W1:','W2:')) else 'FITTED' for e in row['ends']]
        row['status'] = ('FITTED: 14 AWG harness end as built' if row['harness'] in 'AB' else
                         'FITTED: telephone-cord/RJ11 flow tail as built' if row['id'] in {'C09','C10'} else
                         'FITTED: low-voltage wiring as built; end notes retain remaining terminal specifications')
        row['connection_review'] = CONNECTION_REVIEW
        row['design_status'] = DESIGN_STATUS
        row['revision'] = 14
        result.append(row)
    return result
