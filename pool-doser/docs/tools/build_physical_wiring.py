#!/usr/bin/env python3
"""Generate the offline color wiring book, SVGs and both-end termination list.

Run with Python's standard library. PDF export is a separate browser step.
"""
import html
import json
from collections import Counter
from wiring_data import DOCS, STRIPS, INTERNAL_LINKS, SHARED_ENDS, SHARED_CHECKS, PREPARATION, PREP_LABEL, CONNECTION_REVIEW, DESIGN_STATUS, ASSEMBLY_RECORD, BUILD_CHECKS, TERMINAL_WARNING, load_wires

E = html.escape
WIRES = load_wires()
BY_ID = {w['id']: w for w in WIRES}
WIDTH, HEIGHT = 2200, 1400
DEVICES = [('W1', 170, 260, 'MASTER / K2'), ('K1', 520, 175, 'MASTER CONTACTOR'),
           ('W2', 790, 260, 'SLAVE / K3'), ('K5', 1140, 175, 'SLAVE CONTACTOR'),
           ('K4', 1390, 140, 'OUTLET DETECTOR'), ('PS1', 1590, 240, '5 V SUPPLY'),
           ('F1', 1920, 175, '5 A BREAKER')]
POINTS = {}
PINS = {}
for name, x, width, _ in DEVICES:
    if name.startswith('W'):
        pins = {'GND': (x+155, 435), 'VCC': (x+225, 435),
                'NO': (x+40, 745), 'COM': (x+100, 745),
                'B-': (x+160, 745), 'A+': (x+220, 745)}
    elif name in {'K1', 'K5'}:
        pins = {'1': (x+48, 435), '3': (x+130, 435), 'A1': (x+48, 520),
                'A2': (x+48, 650), '2': (x+48, 745), '4': (x+130, 745)}
    elif name == 'K4':
        pins = {pin: (x+70, y) for pin, y in [('A1', 435), ('A2', 520), ('11', 600), ('14', 680), ('12', 760)]}
    elif name == 'PS1':
        # MDR-20: output +V, −V, DC OK on top; input FG, N, L on the bottom.
        pins = {'+V': (x+40, 435), '-V': (x+120, 435), 'DC OK': (x+200, 435),
                'FG': (x+40, 745), 'N': (x+120, 745), 'L': (x+200, 745)}
    else:
        pins = {'LOAD': (x+88, 435), 'LINE': (x+88, 745)}
    PINS[name] = pins
    POINTS.update({f'{name}:{pin}': pos for pin,pos in pins.items()})
POINTS.update({
    'BOX:L': (2090,1190), 'BOX:N': (1990,1190), 'BOX:SW': (2040,1190), 'BOX:G': (1930,1190),
})
ROUTES = {
    'A01': [(2090,925),(2008,925)],
    'A02': [(1885,435),(1885,875),(1790,875)],
    'A05': [(2008,480),(2150,480),(2150,955),(890,955)],
    'A06': [(2008,225),(568,225)],
    'A07': [(2125,435),(2125,285),(455,285),(455,820),(270,820)],
    'A08': [(1880,1190),(1880,260),(1330,260),(1330,435)],
    'A11': [(1270,220),(650,220)],
    'A13': [(1230,435),(1230,340),(1850,340),(1850,830),(1710,830)],
    'A14': [(1270,560),(1335,560),(1335,650)],
    'A15': [(725,435),(725,650)],
    'A16': [(610,435),(610,300),(1558,300),(1558,520)],
    'A17': [(1860,1190),(1860,1050),(1630,1050)],
    'B01': [(210,820),(465,820),(465,520)],
    'B02': [(830,820),(1085,820),(1085,520)],
    'B03': [(568,890),(740,890),(740,270),(1188,270)],
    'B05': [(1225,745),(1225,695),(1350,695),(1350,310),(1460,310)],
    'B04': [(1188,990),(2040,990)],
    'C01': [(1630,240),(1015,240)],
    'C03': [(1085,435),(1085,290),(395,290)],
    'C04': [(1710,225),(945,225)],
    'C06': [(915,435),(915,290),(325,290)],
    'C07': [(390,850),(1010,850)], 'C08': [(330,920),(950,920)],
}
TAGS = {
    'A01': (2050,905), 'A02': (1810,855), 'A05': (1500,950),
    'A06': (1620,205), 'A07': (340,800), 'A08': (1810,240),
    'A11': (955,200), 'A13': (1510,320), 'A14': (1360,610),
    'A15': (670,630), 'A16': (1010,280), 'A17': (1745,1035),
    'B01': (335,802), 'B02': (955,802), 'B03': (945,250),
    'B05': (1410,290), 'B04': (1820,970),
    'C01': (1480,220), 'C03': (710,270),
    'C04': (1500,205), 'C06': (650,270),
    'C07': (685,832), 'C08': (660,902),
}

CSS = '''text{font-family:Arial,Helvetica,sans-serif;fill:#20313e}
.t-title{font-size:36px;font-weight:700;fill:white}.t-sub{font-size:18px;fill:#cedee7}
.t-label{font-size:18px;font-weight:700}.t-small{font-size:16px}.t-tiny{font-size:14px}
.t-micro{font-size:12px}.t-white{fill:white}.t-muted{fill:#677987}
.wire{cursor:pointer}.wire path{fill:none;stroke-linecap:round;stroke-linejoin:round}
.endpoint{cursor:pointer}.wire.muted,.endpoint.muted{opacity:.12}.wire.selected .halo{stroke:#f3bb42;stroke-width:15}
.endpoint.selected .screw{fill:#ffe599;stroke:#916012;stroke-width:4}
.wire:focus,.endpoint:focus{outline:3px solid #e7ac36}.unused{fill:#85939d}
'''

class Canvas:
    def __init__(self, key, number, title, subtitle):
        self.key = key
        self.items = []
        self.rect(0,0,WIDTH,HEIGHT,'#fff')
        self.rect(0,0,WIDTH,105,'#152d3b')
        self.text(35,48,f'{number}  /  {title}', 'title')
        self.text(35,80,subtitle,'sub')
        self.text(2165,46,'POOLDose', 'sub', 'end')
        self.text(2165,77,'REV 14', 'sub', 'end')

    def text(self, x, y, value, cls='small', anchor=None, fill=None):
        extra = (f' text-anchor="{anchor}"' if anchor else '') + (f' style="fill:{fill}"' if fill else '')
        self.items.append(f'<text x="{x}" y="{y}" class="t-{cls}"{extra}>{E(str(value))}</text>')

    def rect(self, x,y,w,h, fill='#f4f7f9', stroke='none', rx=0, extra=''):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" rx="{rx}" {extra}/>')

    def path(self, d, stroke, width=2, extra=''):
        self.items.append(f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="{width}" {extra}/>')

    def circle(self,x,y,r,fill,stroke='none',extra=''):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" {extra}/>')

    def note(self,x,y,w,title,lines):
        self.rect(x,y,w,43+len(lines)*25,'#eef4f7','#cfdae2',8)
        self.text(x+17,y+28,title,'label')
        for i,line in enumerate(lines): self.text(x+17,y+55+i*25,line,'small')

    def hardware(self):
        self.rect(165,551,1945,35,'#dce3e7','#a8b5bf',2)
        self.path('M165 559 H2110 M165 578 H2110','#f9fbfc',3)
        for name,x,w,role in DEVICES:
            self.items.append(f'<g class="device" data-device="{name}" data-x="{x}">')
            self.rect(x,410,w,350,'#edf0ee','#94a3aa',7)
            if name.startswith('W'):
                self.rect(x+15,480,w-30,194,'#29352f','#70826b',4)
                self.rect(x+30,488,7,170,'#9dc33b')
                self.text(x+w/2,516,'WAVESHARE','label','middle','#fff')
                self.text(x+w/2,545,'ESP32-S3','small','middle','#fff')
                self.text(x+w/2,568,'Relay-1CH-U','small','middle','#fff')
                for i,color in enumerate(['#75958e','#94a950','#a25445']): self.circle(x+75+i*55,592,7,color)
                self.text(x+w/2,652,'120 V relay | RS485','tiny','middle','#fff')
                self.rect(x+24,431,65,20,'#263d45','#74868e',9)
                self.text(x+56,465,'USB-C','tiny','middle')
                self.rect(x+27,401,18,17,'#c5a542','#94712c',2)
            elif name in {'K1','K5'}:
                self.rect(x+14,493,w-28,53,'#dce1df',rx=3)
                self.rect(x+14,626,w-28,48,'#dce1df',rx=3)
                self.text(x+w/2,590,'finder','title','middle','#4d626d')
                self.text(x+w/2,614,'22.32.0.120.1320','micro','middle')
            elif name=='K4':
                self.rect(x+99,530,26,172,'#fff','#c6cfcd',2)
                self.rect(x+101,655,28,50,'#f49a21','#cc7b0c',3)
                self.text(x+20,574,'120 V','micro')
            elif name=='PS1':
                self.rect(x+15,485,w-30,202,'#414e56',rx=5)
                self.text(x+w/2,532,'MEAN WELL','label','middle','#fff')
                self.text(x+w/2,565,'MDR-20-5','small','middle','#fff')
                self.text(x+w/2,597,'5 V / 3 A','small','middle','#fff')
                self.text(x+w/2,631,'AC INPUT ↓','tiny','middle','#fff')
                self.text(x+w/2,653,'≥5 mm free each side','micro','middle','#fff')
                self.text(x+w/2,675,'≥40 mm ↑ / ≥20 mm ↓','micro','middle','#fff')
            else:
                self.text(x+w/2,502,'Schneider','label','middle','#248451')
                self.text(x+w/2,531,'60106 · C60','small','middle')
                self.text(x+w/2,560,'5 A / 120 V','small','middle')
                self.rect(x+43,585,90,55,'#25465d','#153246',4)
                self.rect(x+43,585,90,19,'#21949c',rx=3)
                self.text(x+w/2,600,'O - OFF','tiny','middle','#fff')
            self.items.append('</g>')

    def labels(self):
        # Keep headings alongside top entry wires rather than masking the paths.
        positions = {'W1':230,'K1':655,'W2':850,'K5':1275,'K4':1400,'PS1':1765,'F1':2065}
        for name,x,w,role in DEVICES:
            center=positions[name]
            self.text(center,372,name,'label','middle')
            self.text(center,396,'DETECTOR' if name=='K4' else role,'micro','middle')

    def legend(self, text):
        self.text(35,140,text,'label')
        self.text(35,172,'SINGLE = one conductor   ·   SHARED = listed conductors in ONE ferrule   ·   FITTED = as built   ·   DATA = remaining specification','small')

    def wire(self, row, points=None):
        if points is None:
            points=[POINTS[row['ends'][0]],*ROUTES[row['id']],POINTS[row['ends'][1]]]
        d='M'+' L'.join(f'{x},{y}' for x,y in points)
        self.items.append(f'<g class="wire" data-wires="{row["id"]}" tabindex="0" role="button" aria-label="{E(row["id"]+": "+row["color"]+", "+" to ".join(row["physical"]))}">')
        self.items.append(f'<title>{E(row["id"]+" · "+row["color"]+" · "+" → ".join(row["ends"]))}</title>')
        self.path(d,'#fff',12,'class="halo"')
        self.path(d,'#6a7883',8)
        self.path(d,row['hex'],5.5,f'id="wire-{row["id"]}" data-from="{row["ends"][0]}" data-to="{row["ends"][1]}"')
        if row['color']=='White/blue stripe': self.path(d,'#217dcb',3.5,'stroke-dasharray="10 9"')
        self.items.append('</g>')

    def endpoint(self,key,point=None,label_side=None):
        x,y=POINTS[key] if point is None else point
        refs=[(r,i) for r in WIRES if r['sheet']==self.key for i,e in enumerate(r['ends']) if e==key]
        ids=' '.join(r['id'] for r,_ in refs)
        prep='/'.join(dict.fromkeys(r['prep'][i] for r,i in refs))
        statuses = {r['end_status'][i] for r,i in refs}
        status = ('FITTED' if 'FITTED' in statuses else 'DATA' if 'DATA' in statuses
                  else 'CHECK' if ids else '')
        self.items.append(f'<g class="endpoint" data-endpoint="{key}" data-wires="{ids}" data-prep="{prep}" data-status="{status}"><title>{E(key+": "+ids+" "+prep+" "+status)}</title>')
        self.circle(x,y,10,'#fff','#754a96' if key in SHARED_ENDS and ids else '#607583','class="screw" stroke-width="3"')
        if key.startswith(('J','RJ11:')) or key=='R1:SENSE':
            self.circle(x,y,4,'#6b8290')
        else:
            self.path(f'M{x-5} {y+4} l10 -8','#788a94',2)
        label=key if key.startswith('J') else key.split(':')[-1]
        if key.startswith('BOX:'): label={'L':'L OUT','N':'N OUT','SW':'J1 IN','G':'PE OUT'}[key.split(':')[1]]
        label_x=x+65 if label_side=='right' else x
        self.rect(label_x-33,y-34,66,20,'#fff',rx=3)
        self.text(label_x,y-19,label,'tiny' if not key.startswith('BOX:') else 'micro','middle')
        if ids:
            names=ids.split()
            lines=[' '.join(names[i:i+2]) for i in range(0,len(names),2)]
            offset=15*(len(lines)-1)
            self.rect(x-48,y+13,96,30+offset,'#fff',rx=3)
            for i,line in enumerate(lines): self.text(x,y+26+15*i,line,'tiny','middle')
            self.text(x,y+40+offset,PREP_LABEL.get(prep,prep),'micro','middle')
            if status == 'DATA' or (status == 'FITTED' and key in SHARED_ENDS):
                self.rect(x-30,y+45+offset,60,16,'#e4f1ee' if status=='FITTED' else '#fff0d6',rx=3)
                self.text(x,y+57+offset,status,'micro','middle','#0a6856' if status=='FITTED' else '#934f08')
            if prep.startswith(('S','T','M')):
                shared=prep.startswith(('T','M'))
                collar='#d93136' if prep in {'M4_14','M3_14','S18'} else '#f4c627' if prep=='M2_14' else '#949ca5' if shared else '#217dcb'
                self.rect(x+12,y-5,7,10,'#bec6ca','#67757e',1)
                self.rect(x+19,y-9 if shared else y-7,12 if shared else 7,18 if shared else 14,collar,rx=1)
                if shared:
                    self.path(f'M{x+25} {y-7} V{y+7}','#fff',1)
        else:
            if key in {'K1:4','K5:4'}:
                self.text(x,y+28,'no wire','micro','middle','#677987')
                self.text(x,y+43,'N when closed','micro','middle','#677987')
            elif key in {'K4:12','PS1:DC OK'}: self.text(x,y+28,'unused','micro','middle','#84939b')
        self.items.append('</g>')

    def terminals(self):
        for name,pins in PINS.items():
            for pin in pins: self.endpoint(f'{name}:{pin}')

    def box(self):
        self.rect(1890,1120,260,205,'#eef2f4','#7b8b95',12)
        self.text(1908,1150,'METAL BOX','label')
        # Same L/N/SW boundary points on every AC sheet; inactive ones are references only.
        # The ground Wago appears only on sheet 02, where A17 is drawn.
        self.text(2020,1255,'Wagos: source + outlet whips','tiny','middle')
        self.text(2020,1280,'Whips: stranded 14 AWG','tiny','middle')
        self.text(2020,1305,'N and PE remain separate','tiny','middle')

    def draw_wires(self):
        for r in WIRES:
            if r['sheet']==self.key: self.wire(r)
        self.labels()
        self.terminals()
        for r in WIRES:
            if r['sheet']==self.key:
                x,y=TAGS[r['id']]
                self.text(x,y,r['id'],'tiny','middle')

    def finish(self):
        self.text(35,1371,'WIRE BY ID · follow the end-preparation notes and assembly checkout · crossings do not connect','small')
        n=sum(any(r['sheet']==self.key and node in r['ends'] for r in WIRES) for node in SHARED_ENDS)
        self.text(2165,1371,f'{len(SHARED_ENDS)} requested shared ferrules' if self.key in {'overview','ferrules'} else f'This sheet: {n} shared ferrule'+('s' if n!=1 else ''),'label','end')
        return '\n'.join(self.items)


def overview():
    c=Canvas('overview','01','Three harnesses. Four box wires.','Ferrule branches at existing devices · all 120 V wires are 14 AWG · small AC terminals each receive one blue ferrule')
    c.legend('A  AC supply   /   B  Switching   /   C  Low voltage      ·      device order matches the enclosure photo')
    c.note(45,215,1650,'Ferrules and checkout', [
        'Three LARGE RED ferrules: F1 LOAD, K5-3 and K1-3. One YELLOW: K5-2. Remaining 14 AWG device ends: BLUE.',
        'Check: supply output 5.00 V with all relays closed; communication, timed run, STOP and flow-loss shutdown.',
        'Calibrate with water before chemical service (docs/pump-calibration.md). Assembly and checkout: docs/build-instructions.html.',
        'WARNING: K5-3 and K1-3 exceed the Finder terminal rating. Reference build only; do not copy. See docs/wiring-schedule.md.'])
    c.note(1730,215,415,'Finder operating power',[
        'A1/A2: 120 V AC coil supply.',
        '1-2 and 3-4: NO contact pairs.',
        'LED + mechanical indicator.',
        'Wiring scope: see end notes.'])
    c.hardware();c.labels();c.terminals()
    for x,name,lines in [
        (50,'A — AC SUPPLY',['Black hot, white neutral, green PS1 ground.', 'Larger screws carry the branch ferrules.', 'Sheets 02–03.']),
        (475,'B — SWITCHING',['Red coil leads + outlet return.', 'K5-2 shares B04 + B05.', 'Sheet 04.']),
        (900,'C — LOW VOLTAGE',['5 V, RS485 + GPIO tails.', 'PS1 → W2 → W1 power chains.', 'Sheets 05–07.'])]:
        c.note(x,850,400,name,lines)
    c.note(50,1030,1250,'Only these leads cross the metal-box boundary',[
        'A01  BLACK / 14 AWG   OUT → F1 bottom LINE.  A02 leaves the top LOAD terminal.',
        'A08  WHITE / 14 AWG   OUT → K5 POWER terminal 3. Both whip neutrals join inside the box.',
        'B04  RED / 14 AWG       IN ← final switched output.  Wago → outlet-whip BLACK.',
        'A17  GREEN / 14 AWG   OUT → Mean Well PS1 FG. Ground Wago with both whip grounds.',
        'Two 3-wire whips end in the box; all whip and 120 V harness wires are stranded 14 AWG.',
        'Whip grounds are joined and bonded to the metal box.'])
    c.rect(1380,850,765,430,'#edf2f5','#8194a1',16)
    c.text(1410,890,'METAL JUNCTION BOX','label')
    c.text(1410,923,'Wagos join the source/outlet whips to four control leads.','small')
    # These are functional internal references, not additional harness cut lengths.
    for y,color,label in [(967,'#202832','A01 · BLACK OUT → F1 bottom LINE'),(1012,'#fff','A08 · WHITE OUT → controls'),(1057,'#d93136','B04 · RED IN → outlet-whip BLACK'),(1102,'#26834b','A17 · GREEN OUT → PS1 FG')]:
        c.path(f'M1400 {y} H1600','#6a7883',10);c.path(f'M1400 {y} H1600',color,7)
        c.text(1625,y+6,label,'small')
    c.path('M1420 1160 H1740','#6a7883',10);c.path('M1420 1160 H1740','#fff',6)
    c.text(1760,1166,'Neutral → outlet whip','small')
    c.path('M1420 1210 H1740 M1570 1210 V1240 H1740','#17643c',6)
    c.text(1760,1216,'Ground → outlet whip','small')
    c.text(1760,1246,'PE → metal-box bond','small')
    c.text(35,1324,'Neutral and PE are separate nets. Green A17 is the only harness PE lead: ground Wago → PS1 FG.','label')
    return c.finish()


def mains():
    c=Canvas('mains','02','Harness A / BLACK hot branches + GREEN ground','F1 top LOAD: one ferrule / four 14 AWG wires → PS1 L, W2 COM, K1 terminal 1 and W1 COM · A17 box ground → PS1 FG')
    c.legend('BLACK = stranded 14 AWG   ·   GREEN A17 = PE   ·   every destination is SINGLE BLUE   ·   Waveshare COM is the relay contact, never VCC')
    c.hardware();c.box();c.draw_wires()
    for key in ('BOX:L','BOX:N','BOX:SW','BOX:G'): c.endpoint(key)
    c.note(45,1080,860,'Four individual leads leave ONE ferrule at F1 LOAD',[
        'A02 → Mean Well L. A05 → Slave W2 COM.',
        'A06 → Master Finder K1 terminal 1. A07 → Master W1 COM.',
        'Each far end is one 14 AWG wire in one blue ferrule.',
        'A01 is the single incoming black lead at bottom LINE.',
        'A17 green: box ground Wago → PS1 FG, single blue.'])
    c.note(955,1080,855,'Branch ferrule',[
        'One LARGE RED ferrule at F1 top LOAD.',
        'A02/A05/A06/A07: four 14 AWG wires in that one end.',
        'Tighten and check each wire for retention.',
        'Kit chart: 8 AWG ferrule label / 12 mm barrel / 4.9 mm OD.'])
    return c.finish()


def neutral():
    c=Canvas('neutral','03','Harness A / WHITE neutral branches','BOX N → Finder K5 terminal 3 → Finder K1 terminal 3 · separate single-blue leads to all four neutral loads')
    c.legend('WHITE = stranded 14 AWG   ·   branch at POWER terminal 3   ·   all A1/A2 and Mean Well N ends are SINGLE BLUE')
    c.hardware();c.box();c.draw_wires()
    for key in ('BOX:L','BOX:N','BOX:SW'): c.endpoint(key)
    for x,title,lines in [
        (45,'K1 POWER terminal 3 / three wires', ['A11 IN + A15 to K1 A2 + A16 to K4 A2.', 'Over Finder rating: reference build only.', 'K1 A2 gets only A15 in one blue ferrule.']),
        (660,'K5 POWER terminal 3 / four wires', ['A08 IN + A11 bridge + A13 + A14.', 'A13 → PS1 N. A14 → K5 A2; each single blue.', 'Over Finder rating: reference build only.']),
        (1275,'No neutral path crosses a contact', ['A11 is a real wire between the two terminal 3s.', 'Both terminal 4s have no external wire.', 'A closed 3–4 contact only makes its empty 4 neutral.'])]:
        c.note(x,1120,580,title,lines)
    c.text(45,1320,'A1/A2 are coil terminals, distinct from 1/2/3/4. Both coils retain neutral in every contactor state.','label')
    return c.finish()


def switched():
    c=Canvas('switched','04','Harness B / switched wires','B01–B05 · all RED 14 AWG · independent coil jumpers and series pump-power path')
    c.legend('B01 = Master → K1 coil   ·   B02 = Slave → K5 coil   ·   B03 = series power link   ·   B04/B05 = final output + detector')
    c.hardware();c.box();c.draw_wires()
    for key in ('BOX:L','BOX:N','BOX:SW'): c.endpoint(key)
    c.note(45,210,640,'What the detector measures',[
        'B04 feeds pump hot; B05 senses the same switched hot.',
        'K4 senses red A1 relative to white A2 (sheet 03).'])
    c.note(45,1110,855,'Finder coil power: A1/A2 are distinct from 1/2',[
        'B01: W1 NO → K1 A1. B02: W2 NO → K5 A1.',
        'Both A2 terminals receive white neutral from harness A.',
        '1-2 switches pump hot. Terminal 3 is neutral; 4 has no wire.',
        'The LED follows coil power; indicators need no extra wiring.'])
    c.rect(935,1110,875,220,'#eef4f7','#cfdae2',8)
    c.text(952,1138,'Both red wires share ONE ferrule at Finder K5 terminal 2','label')
    c.text(1010,1170,'K5 2','small','middle')
    c.rect(1020,1182,98,26,'#bec6ca','#67757e',2)
    c.rect(1118,1170,44,50,'#f4c627','#67757e',3)
    c.circle(1010,1195,15,'#fff','#607583','stroke-width="3"')
    c.path('M1003 1201 l14 -12','#788a94',3)
    for y,label in [(1180,'B04 → in-box Wago → outlet-whip BLACK'),(1210,'B05 → detector K4 A1')]:
        c.path(f'M1162 {y} H1320','#6a7883',8)
        c.path(f'M1162 {y} H1320','#d93136',5.5)
        c.text(1340,y+5,label,'small')
    c.text(952,1257,'Detector A1 receives B05 alone: SINGLE 14.','small')
    c.text(952,1282,'Its A1 and A2 each receive one 14 AWG wire in one ferrule.','small')
    c.text(952,1307,'K5-2: one YELLOW ferrule holds both red 14 AWG wires.','small')
    return c.finish()


def dc():
    c=Canvas('dc','05','Harness C / RED +5 V daisy chain','Two RED 18 AWG segments · PS1 +V → Slave VCC → Master VCC')
    c.legend('5 V DC ONLY   ·   C01 arrives and C03 continues from the SAME W2 VCC screw')
    c.hardware();c.draw_wires()
    c.note(45,900,900,'The red low-voltage harness',[
        'C01: PS1 +V → W2 VCC.',
        'C03: same W2 VCC → W1 VCC.',
        'W2 VCC: C01 + C03 together in ONE 2 × 18 AWG twin.',
        'PS1 +V and W1 VCC each have one single-ferruled wire.'])
    c.note(1030,900,1110,'Existing terminal checks',[
        'DATA: record Waveshare single/twin wire ranges, strip and torque.',
        'PS1 stays single on +V; DC OK stays unused.',
        'Use the top VCC screw, not GPIO 3V3.',
        'The requested shared end is visible at W2; no added connector is drawn.'])
    return c.finish()


def returns():
    c=Canvas('returns','06','Harness C / BLACK 0 V daisy chain + RS485','Two BLACK 18 AWG segments · PS1 −V → Slave GND → Master GND')
    c.legend('BLACK = DC 0 V, separate from AC neutral and PE   ·   BLUE = A+   ·   WHITE/BLUE = B−')
    c.hardware();c.draw_wires()
    c.note(45,1040,940,'Intact Cat6A cable',[
        'C07 blue: A+ ↔ A+. C08 white/blue: B− ↔ B−.',
        'Preserve the cable jacket and pair twist.',
        'Enable both 120 Ω jumpers.',
        'Check cable gauge/construction; solid conductors stay bare.'])
    c.note(1060,1040,1080,'The black low-voltage harness',[
        'C04: PS1 −V → W2 GND. C06: same W2 GND → W1 GND.',
        'W2 GND: C04 + C06 together in ONE 2 × 18 AWG twin.',
        'PS1 −V and final W1 GND each have one single-ferruled wire.',
        'DATA: record the Waveshare terminal/ferrule specification.'])
    return c.finish()


def signals():
    c=Canvas('signals','07','Harness C / GPIO plugs and pull-up','Photographed local pull-up splice · ORANGE detector signal / BLACK return · completed RED/GREEN telephone-cord flow tail')
    c.note(45,125,2110,'J5 plug colors: 1 YELLOW / GPIO1 · 2 ORANGE / GPIO2 · 3 RED / +3.3 V · 4 BLACK / GND',[
        'Slave: RED → R1 → one splice with YELLOW GPIO1 + ORANGE C13. BLACK → BLACK C14. Insulate each joint separately.',
        'C13 alone → detector 14; C14 alone → detector 11. Both are single-wire ends. The thin ORANGE GPIO2 lead stays unused.'])
    points={
        'JR1':(1000,660),
        'R1:SENSE':(1320,660), 'JS2':(1450,660), 'JS3':(700,960),
        'K4:11':(2000,400), 'K4:14':(2000,740), 'K4:12':(2000,890),
        'JM1':(680,1140), 'JM2':(680,1240), 'RJ11:FLOW':(1170,1140), 'RJ11:RETURN':(1170,1240),
    }
    routes={
        'C09':[], 'C10':[], 'C12':[],
        'C13':[(1790,660),(1790,740)],
        'C14':[(2160,960),(2160,400)],
    }
    # Only the GPIO plugs and detector contacts appear in this enlarged view.
    # The physical rail order remains on the other sheets.
    c.items.append('<g class="device" data-device="W2" data-x="45">')
    c.rect(45,265,445,760,'#edf2f5','#8194a1',12)
    c.text(75,302,'W2 / SLAVE · four-wire J5 plug','label')
    c.text(75,327,'Photo layout · orange signal / black return','tiny')
    for y,label in [(370,'1 · GPIO1 · YELLOW'),(500,'2 · GPIO2 · ORANGE'),
                    (660,'3 · +3.3 V · RED'),(960,'4 · GND · BLACK')]:
        c.text(75,y+6,label,'label')
    c.items.append('</g>')
    c.items.append('<g class="device" data-device="K4" data-x="1880">')
    c.rect(1880,265,250,675,'#edf2f5','#8194a1',12)
    c.text(2005,302,'K4 / DETECTOR','label','middle')
    c.text(2005,327,'Contact terminals only','tiny','middle')
    c.text(2005,485,'11 = common','small','middle')
    c.text(2005,514,'14 = normally open','small','middle')
    c.text(2005,560,'11–14 closes when','tiny','middle')
    c.text(2005,585,'the outlet is powered.','tiny','middle')
    c.items.append('</g>')
    c.items.append('<g class="device" data-device="W1" data-x="45">')
    c.rect(45,1060,2110,275,'#f0f5f7','#8194a1',12)
    c.text(75,1093,'W1 / MASTER · completed flow tail','label')
    for y,label in [(1140,'1 · GPIO1 · YELLOW'),(1170,'2 · GPIO2 · ORANGE'),
                    (1200,'3 · +3.3 V · RED'),(1240,'4 · GND · BLACK')]:
        c.text(75,y+6,label,'small')
    c.items.append('</g>')
    c.rect(800,1128,245,125,'#e1e6e9','#a7b6bf',8)
    c.rect(1100,1105,190,188,'#e5edf1','#8194a1',5)
    c.text(1195,1093,'RJ11 ↔ RJ11 · enclosure bottom','small','middle')

    def factory_lead(board,pin,signal,y,start,end,joint=''):
        color={1:'#f4c627',2:'#ed7c19',3:'#d93136',4:'#202832'}[pin]
        c.items.append(f'<g data-gpio-board="{board}" data-gpio-pin="{pin}" data-signal="{signal}" data-joint="{joint}" data-unused="{str(not joint).lower()}">')
        d=f'M{start},{y} L{end},{y}'
        if joint and points[joint][1]!=y:
            d+=f' L{end},{points[joint][1]}'
        c.path(d,'#fff',12)
        c.path(d,'#6a7883',8)
        c.path(d,color,5.5,'class="factory-lead"')
        c.circle(start,y,6,'#fff','#596b79')
        if not joint:
            c.rect(end-8,y-8,20,16,'#667986',rx=3)
        c.items.append('</g>')

    for board,start,pins in [
        ('W2',490,[(1,'GPIO1',370,1450,'JS2'),(2,'GPIO2',500,650,''),
                   (3,'3V3',660,1000,'JR1'),(4,'GND',960,700,'JS3')]),
        ('W1',480,[(1,'GPIO1',1140,680,'JM1'),(2,'GPIO2',1170,550,''),
                   (3,'3V3',1200,550,''),(4,'GND',1240,680,'JM2')]),
    ]:
        for pin,signal,y,end,joint in pins:
            factory_lead(board,pin,signal,y,start,end,joint)
    c.text(745,506,'Cap this ORANGE plug wire individually. GPIO2 has no connection.','label')
    c.text(570,1190,'Cap separately','micro')
    c.items.append('<g data-component="R1" data-from="JR1" data-to="R1:SENSE" data-ohms="270">')
    c.rect(1070,642,140,36,'#ead9b3','#9d824b',3)
    c.path('M1000 660 H1070 M1210 660 H1320','#8896a0',4)
    c.text(1140,603,'R1 · 270 Ω · ¼ W','label','middle')
    c.items.append('</g>')
    c.text(1000,704,'RED lead joined directly to R1','small','middle')
    c.text(1140,737,'Either resistor lead may face 3.3 V.','small','middle')
    c.text(1140,762,'C12 is the resistor’s own lead; sleeve it.','small','middle')
    for row in WIRES:
        if row['sheet']=='signals':
            c.wire(row,[points[row['ends'][0]],*routes[row['id']],points[row['ends'][1]]])
    for key,point in points.items():
        c.endpoint(key,point,label_side='right' if key=='JS2' else None)
    for x,y,label in [
        (1030,347,'YELLOW GPIO1 joins the resistor/output splice at JS2'),
        (1685,635,'C13 · ORANGE · to K4-14'), (1230,938,'C14 · BLACK · to K4-11'),
        (930,1120,'C09 · RED telephone wire'), (930,1220,'C10 · GREEN telephone wire'),
    ]:
        c.text(x,y,label,'small','middle')
    c.text(760,862,'JS2: YELLOW GPIO1 + R1 lead C12 + ORANGE C13 in one local splice.','label')
    c.text(760,890,'K4-14 and K4-11 each receive ONE 18 AWG wire in a SINGLE ferrule.','small')
    c.text(760,918,'Insulate JS2, the RED/R1 splice and the BLACK splice separately; support the assembly.','small')
    for y in (1140,1240):
        c.path(f'M1170 {y} H1510','#fff',9)
        c.path(f'M1170 {y} H1510','#596b79',4)
        c.circle(1510,y,6,'#fff','#617685')
    c.path('M1510 1240 L1538 1165','#617685',3)
    c.text(1580,1148,'S1 · generic Hayward-style flow switch','label')
    c.text(1580,1180,'RJ11 plug connects to the other side.','small')
    c.text(1580,1220,'Keep the installed contact mapping.','small')
    c.text(750,1305,'Photos: YELLOW GPIO1 → RED cord wire; BLACK GND → GREEN cord wire. Master tail is shown finished in heat-shrink.','small')
    c.text(75,1042,'Unplug GPIO plugs for soldering and resistance checks. Slave pins 3 ↔ 1: 270 Ω ±5%; pins 1 ↔ 4: open with K4 released.','small')
    return c.finish()


def ferrules():
    c=Canvas('ferrules','08','One ferrule / multiple conductors','Six shared device locations · K4 detector terminals 11 and 14 now each receive a SINGLE 18 AWG wire')
    cards=[(45,'F1 top LOAD','A02 + A05 + A06 + A07',4,'#202832'),
           (755,'K5 POWER terminal 3','A08 + A11 + A13 + A14',4,'#fff'),
           (1465,'K1 POWER terminal 3','A11 + A15 + A16',3,'#fff')]
    for x,title,ids,count,color in cards:
        c.rect(x,140,680,395,'#f1f5f7','#c6d3dc',10)
        c.text(x+25,180,title+f' / {count} × 14 AWG','label')
        c.text(x+25,217,ids,'label')
        c.rect(x+430,270,175,100,'#dce5ea','#7d929e',5)
        c.circle(x+530,320,19,'#aebdc6','#65808f');c.path(f'M{x+517} 331 L{x+543} 309','#425e70',4)
        for y in ([284,308,332,356] if count==4 else [296,320,344]):
            c.path(f'M{x+60} {y} H{x+310}','#667986',14)
            c.path(f'M{x+60} {y} H{x+310}',color,9)
        c.rect(x+288,267,52,108,'#d93136','#668093',5)
        c.rect(x+340,279,107,82,'#bcc9d1','#6c8290',3)
        c.text(x+350,409,f'ONE crimped end / {count} separate conductors','small','middle')
        c.text(x+25,452,'LARGE RED / 12 mm kit barrel.','small',fill='#934f08')
        c.text(x+25,489,'Single blue at each small-terminal destination.','small')
        if 'POWER terminal 3' in title: c.text(x+25,522,'Exceeds Finder rating: reference build only.','label',fill='#b3261e')
    rows=[
        ('F1 LOAD','A02 + A05 + A06 + A07','BLACK · 4 × 14','FITTED — LARGE RED; tightened and connected as drawn.'),
        ('K5 terminal 3','A08 + A11 + A13 + A14','WHITE · 4 × 14','FITTED — LARGE RED; exceeds Finder rating, reference build only.'),
        ('K1 terminal 3','A11 + A15 + A16','WHITE · 3 × 14','FITTED — LARGE RED; exceeds Finder rating, reference build only.'),
        ('K5 terminal 2','B04 + B05','RED · 2 × 14','FITTED — YELLOW; two 14 AWG wires in the one kit ferrule.'),
        ('W2 VCC','C01 + C03','RED · 2 × 18','DATA — record twin size/profile within the blue-sized envelope.'),
        ('W2 GND','C04 + C06','BLACK · 2 × 18','DATA — record twin size/profile within the blue-sized envelope.'),
    ]
    c.text(55,580,'EXISTING SCREW','label');c.text(355,580,'ALL WIRES IN ONE FERRULE','label')
    c.text(700,580,'INSULATION / AWG','label');c.text(1050,580,'TERMINAL NOTE','label')
    for i,(a,b,color,status) in enumerate(rows):
        y=600+i*65;c.rect(40,y,2110,61,'#eef4f7' if i%2==0 else '#fff',rx=4)
        c.text(55,y+36,a,'label');c.text(355,y+36,b,'small');c.text(700,y+36,color,'small');c.text(1050,y+36,status,'small')
    c.note(45,1110,2100,'14 AWG ferrules: three large red, one yellow, remaining device ends blue',[
        'Mean Well L/N/FG, Waveshare COM/NO, detector A1/A2 and all Finder A1/A2: one 14 AWG wire in one blue ferrule.',
        '14 AWG groups: two four-wire red, one three-wire red, one two-wire yellow and 17 single blue ends.',
        'Kit chart: large red = 8 AWG label / 12 mm / 4.9 mm OD; yellow = 10 AWG label / 12 mm / 3.9 mm OD.',
        'K4-14: one orange C13. K4-11: one black C14. The GPIO1/R1 branch is soldered locally at JS2; no shared ferrule at K4.',
        'Finder 22.32 contact terminals are rated for 1 × 10 AWG or 2 × 12 AWG. K5-3 and K1-3 exceed that; use a rated neutral terminal block instead.'])
    return c.finish()


def svg_document(body,title,height=HEIGHT):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" viewBox="0 0 {WIDTH} {height}" role="img" aria-label="{E(title)}">
<title>{E(title)}</title><desc>Harness revision 14: ferrule branches at F1 LOAD, K5 power terminal 3 and K1 power terminal 3. One ferrule enters the clamp at each shared endpoint. Small AC terminals each receive one 14 AWG wire in a blue ferrule. Rail order W1, K1, W2, K5, K4, PS1, F1; sheet 07 is an enlarged GPIO connection view. Four box leads: black A01, white A08, red B04, green A17 to the PS1 FG terminal.</desc><style>{CSS}</style>{body}</svg>'''


def main():
    sheets=[('overview','Start / three harnesses',overview()),('mains','A / BLACK hot + ground',mains()),
            ('neutral','A / WHITE neutral',neutral()),('switched','B / switching',switched()),('dc','C / RED +5 V',dc()),('returns','C / BLACK return + data',returns()),
            ('signals','C / GPIO + pull-up',signals()),('ferrules','Shared ferrules',ferrules())]
    for key,title,body in sheets:
        (DOCS/f'pooldose-{key}-wiring.svg').write_text(svg_document(body,title)+'\n')
    combined='\n'.join(f'<g transform="translate(0 {i*HEIGHT})">{body}</g>' for i,(_,_,body) in enumerate(sheets))
    (DOCS/'pooldose-wiring-diagram.svg').write_text(svg_document(combined,'PoolDose complete harness book',HEIGHT*len(sheets))+'\n')
    rows=[]
    md=['# PoolDose end-by-end termination schedule — revision 14',
        '', 'Generated from the revision-14 harness schedule.',
        '', '> [!WARNING]', '> '+TERMINAL_WARNING,
        '', '**28 IDs / 56 ends; six shared ferrules.**',
        BUILD_CHECKS,
        'Three large-red ferrules at F1 LOAD/K5-3/K1-3; one yellow at K5-2; all remaining 14 AWG device ends blue.',
        'Master flow tail: yellow GPIO1 to RED telephone C09; black GND to GREEN telephone C10. Slave: local R1/GPIO1 splice to one ORANGE C13, with BLACK C14 return. C11/C15 are retired.',
        'Existing devices only; all 120 V wires stay 14 AWG. One blue ferrule at every small AC terminal.',
        'Four stranded 14 AWG control leads at the in-box Wagos: A01 black OUT, A08 white OUT, B04 red IN, A17 green OUT to the PS1 FG terminal. Two separate stranded 14 AWG whips connect source power and the pump outlet to this box; their neutrals join and their grounds are already bonded there.',
        '', 'S14/S18 = SINGLE; M2_14/M3_14/M4_14 = TWO/THREE/FOUR 14 AWG conductors in one kit ferrule; T18 = two 18 AWG conductors in one twin; RJ11 = installed phone contact. FITTED = as built; DATA = specifications to record; CHECK = end preparation.',
        'Installed 14 AWG ferrules: three large red/12 mm, one yellow/12 mm and 17 single blue/8 mm. Low-voltage single 18 AWG uses small red/8 mm. Collar color is not insulation color.',
        '', '| Wire | Insulation / conductor | End A | A treatment | End B | B treatment |','|---|---|---|---|---|---|']
    for r in WIRES:
        a,b=r['prep'];swatch=f'<span class="swatch" style="--wire:{r["hex"]}" data-color="{E(r["color"])}"></span>'
        rows.append(f'<tr data-wire="{r["id"]}" tabindex="0"><th scope="row">{r["id"]}</th><td>{swatch}<strong>{E(r["color"])}</strong><br>{E(r["gauge"])}</td><td><b>{E(r["ends"][0])}</b><br>{E(r["physical"][0])}</td><td><span class="endcode">{PREP_LABEL[a]}</span><br>{E(r["prep_notes"][0])}</td><td><b>{E(r["ends"][1])}</b><br>{E(r["physical"][1])}</td><td><span class="endcode">{PREP_LABEL[b]}</span><br>{E(r["prep_notes"][1])}</td></tr>')
        md.append(f'| {r["id"]} | {r["color"]}; {r["gauge"]} | {r["ends"][0]} — {r["physical"][0]} | {a} — {r["prep_notes"][0]} | {r["ends"][1]} — {r["physical"][1]} | {b} — {r["prep_notes"][1]} |')
    md += ['','## Shared ferrules and installation record','','| Terminal | All wire ends in ONE ferrule | Terminal status |','|---|---|---|']
    md += [f'| {node} | '+ ' + '.join(ids) + ' | '+SHARED_CHECKS[node]+' |' for node,ids in SHARED_ENDS.items()]
    md += ['','## End codes','','| Code | Treatment |','|---|---|']+[f'| {code} | {s} |' for code,s in PREPARATION.items()]
    counts=Counter(p for r in WIRES for p in r['prep'])
    md += ['','## Counts','']+[f'- {code}: {n} ends.' for code,n in sorted(counts.items())]
    md += ['','C12 is an integral R1 lead, not an extra cut length. Factory pigtails and retained box wiring are described separately.',
           'See [assembly instructions](build-instructions.html) for the terminal preparation and checkout procedure.','']
    (DOCS/'termination-schedule.md').write_text('\n'.join(md))
    (DOCS/'wiring-hardware.json').write_text(json.dumps({'revision':14,'connection_review':CONNECTION_REVIEW,'design_status':DESIGN_STATUS,'assembly_record':ASSEMBLY_RECORD,'strips':STRIPS,'internal_links':INTERNAL_LINKS,'requested_shared':SHARED_ENDS,'shared_checks':SHARED_CHECKS},indent=2)+'\n')
    (DOCS/'wiring-connections.json').write_text(json.dumps(WIRES,indent=2,ensure_ascii=False)+'\n')
    template=(DOCS/'tools/wiring-viewer.html').read_text()
    substitutions={
        '@@SHEETS@@':'\n'.join(f'<section class="drawing-sheet" id="sheet-{key}" data-sheet="{key}" aria-label="{E(title)}"><div class="map">{svg_document(body,title)}</div></section>' for key,title,body in sheets),
        '@@TABS@@':''.join(f'<button data-show="{key}" aria-pressed="{str(key=="overview").lower()}">{i+1:02} · {E(title)}</button>' for i,(key,title,_) in enumerate(sheets)),
        '@@ROWS@@':'\n'.join(rows),
        '@@OPTIONS@@':''.join(f'<option value="{r["id"]}">{r["id"]} · {E(r["color"])}</option>' for r in WIRES),
        '@@DATA@@':json.dumps(WIRES,ensure_ascii=False),'@@PREP@@':json.dumps(PREPARATION,ensure_ascii=False),
        '@@BUILD_CHECKS@@':E(BUILD_CHECKS),'@@TERMINAL_WARNING@@':E(TERMINAL_WARNING),
    }
    for token,value in substitutions.items(): template=template.replace(token,value)
    (DOCS/'physical-wiring.html').write_text(template)
    # A bench worksheet includes length blanks instead of invented photo-derived measurements.
    cut=['# Harness cutting and measurement worksheet — revision 14',
         '', 'Use this worksheet for measurement records. Blank length/continuity fields are not test results.',
         '', BUILD_CHECKS,
         '', 'Measure each route on the de-energized assembly. Reference crimper: Sopoby HSC8 6-4; record the actual tool range for each end.',
         'Inventory: stranded hookup wire in 10 ft lengths per color; A01 uses stranded 14 AWG black.',
         'C07/C08 are one intact cable length; C12 is an existing resistor lead. Every intended shared group is listed in the termination schedule.','',
         'C09/C10 are installed red/green telephone-cord conductors. C13/C14 are orange/black detector wires. C11/C15 are retired.','',
         '| Wire | Color / gauge | From → To | Measured cut length | Ends labeled | Continuity |','|---|---|---|---|---|---|']
    for r in WIRES:
        length=('Integral lead' if r['id']=='C12' else 'Installed telephone cord' if r['id'] in {'C09','C10'} else 'length ____' if r['id'] in {'C13','C14'} else '____')
        cut.append(f'| {r["id"]} | {r["color"]} / {r["gauge"]} | {r["from"]} → {r["to"]} | '+length+' | ☐ | ☐ |')
    cut += ['','Before crimping, total the measured lengths per gauge/color and compare with stock. Include service loops and termination allowance.',
            'All onward harness links are real scheduled wires at device screws. The two whips and their Wago connections stay at the metal box.',
            'Record end-preparation and continuity measurements here. Follow the [assembly instructions](build-instructions.html) for checkout and [pump calibration](pump-calibration.md) for the water test.','']
    (DOCS/'harness-cut-list.md').write_text('\n'.join(cut))
    print(f'Built {len(sheets)} sheets, {len(WIRES)} connections / {sum(counts.values())} ends, 3 harnesses, {len(SHARED_ENDS)} shared nodes; connection review and terminal notes recorded separately.')

if __name__=='__main__': main()
