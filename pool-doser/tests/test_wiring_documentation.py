"""Harness connectivity, terminal occupancy and drawing consistency checks."""
import json
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'docs/tools'))
from wiring_data import STRIPS, INTERNAL_LINKS, SHARED_ENDS, SHARED_CHECKS, CONNECTION_REVIEW, DESIGN_STATUS, ASSEMBLY_RECORD, load_wires

wires=load_wires()
by_id={w['id']:w for w in wires}
expected={f'A{i:02}' for i in (1,2,5,6,7,8,11,13,14,15,16,17)}|{f'B{i:02}' for i in range(1,6)}|{f'C{i:02}' for i in range(1,16) if i not in (2,5,11,15)}
assert len(wires)==len(by_id)==28 and set(by_id)==expected
assert json.loads((ROOT/'docs/wiring-connections.json').read_text())==wires
assert all(w['gauge']=='14 AWG MTW, 600 V' for w in wires if w['harness'] in 'AB')
assert all('18 AWG' in w['gauge'] for w in wires if w['harness']=='C' and w['id'] not in {'C07','C08','C09','C10','C12'})
assert by_id['C09']['ends']==['JM1','RJ11:FLOW']
assert by_id['C10']['ends']==['JM2','RJ11:RETURN']
assert all('telephone-cord' in by_id[i]['gauge'] and by_id[i]['end_status']==['FITTED','FITTED'] for i in ('C09','C10'))
assert by_id['C12']['gauge']=='Integral R1 lead'
assert by_id['C07']['color']=='Blue' and by_id['C08']['color']=='White/blue stripe'
assert by_id['C09']['color']=='Red' and by_id['C10']['color']=='Green'
assert by_id['C13']['color']=='Orange' and by_id['C14']['color']=='Black'
assert by_id['C13']['ends']==['JS2','K4:14']
assert not any('?' in p for w in wires for p in w['prep'])

# Exactly four harness conductors at the inner metal box. The only PE lead is
# green A17 to the Mean Well FG terminal; no additional neutral return is
# quietly introduced by a drawing revision.
box={w['id']:(next(e for e in w['ends'] if e.startswith('BOX:')),w['color']) for w in wires if any(e.startswith('BOX:') for e in w['ends'])}
assert box=={'A01':('BOX:L','Black'),'A08':('BOX:N','White'),'B04':('BOX:SW','Red'),'A17':('BOX:G','Green')}
assert by_id['A17']['ends']==['BOX:G','PS1:FG'] and by_id['A17']['sheet']=='mains'
assert [w['id'] for w in wires if w['color']=='Green' and w['harness']=='A']==['A17']
assert not any('PE' in e or 'FEED:' in e for w in wires for e in w['ends'])

# Every shared group lives at a photographed device screw. Requested placement
# and the end notes retain the dimensions/specifications to record.
occupancy=defaultdict(list)
for w in wires:
    for end in w['ends']: occupancy[end].append(w['id'])
assert {e:tuple(ids) for e,ids in occupancy.items() if ':' in e and len(ids)>1}==SHARED_ENDS
assert len(SHARED_ENDS)==6
assert occupancy['K4:14']==['C13'] and occupancy['K4:11']==['C14']
assert occupancy['JS2']==['C12','C13']  # Third conductor is the factory GPIO1 lead.
assert all(len(ids)<=4 for ids in occupancy.values())
assert not STRIPS and not INTERNAL_LINKS
hardware=json.loads((ROOT/'docs/wiring-hardware.json').read_text())
assert hardware['strips']=={} and hardware['internal_links']==[]
assert hardware['requested_shared']=={k:list(v) for k,v in SHARED_ENDS.items()}
assert hardware['shared_checks']==SHARED_CHECKS
assert hardware['connection_review']==CONNECTION_REVIEW
assert hardware['design_status']==DESIGN_STATUS
assert hardware['assembly_record']==ASSEMBLY_RECORD
assert all(w['sheet']=='neutral' and w['gauge']=='14 AWG MTW, 600 V'
           and set(w['prep'])<= {'S14','M3_14','M4_14','BOX'} for w in wires if w['color']=='White')
assert sum(w['sheet']=='neutral' for w in wires)==6
assert occupancy['K4:A2']==['A16']
assert occupancy['K4:A1']==['B05']
assert occupancy['K5:2']==['B04','B05']
assert by_id['B04']['ends']==['K5:2','BOX:SW']
assert by_id['B05']['ends']==['K5:2','K4:A1']
for terminal in ('PS1:L','PS1:N','PS1:FG','W1:COM','W1:NO','W2:COM','W2:NO',
                 'K1:A1','K1:A2','K5:A1','K5:A2','K4:A1','K4:A2'):
    assert len(occupancy[terminal])==1,(terminal,occupancy[terminal])
    row=by_id[occupancy[terminal][0]]
    assert row['prep'][row['ends'].index(terminal)]=='S14'
for wire,terminal in [('A16','K4:A2'),('B05','K4:A1')]:
    assert by_id[wire]['prep'][by_id[wire]['ends'].index(terminal)]=='S14'
for node,ids in SHARED_ENDS.items():
    code='T18' if ids[0].startswith('C') else f'M{len(ids)}_14'
    assert all(by_id[i]['prep'][by_id[i]['ends'].index(node)]==code for i in ids)
data_nodes={node for node,note in SHARED_CHECKS.items() if note.startswith('DATA:')}
assert data_nodes=={'W2:VCC','W2:GND'}
fitted_nodes={node for node,note in SHARED_CHECKS.items() if note.startswith('FITTED:')}
assert fitted_nodes=={'F1:LOAD','K1:3','K5:3','K5:2'}
for node in data_nodes | fitted_nodes:
    status=SHARED_CHECKS[node].split(':')[0]
    for wire in SHARED_ENDS[node]:
        row=by_id[wire];end=row['ends'].index(node)
        assert row['end_status'][end]==status
        assert row['prep_notes'][end].startswith(status+':')
        assert row['connection_review']==CONNECTION_REVIEW
        assert row['design_status']==DESIGN_STATUS
counts=Counter(p for w in wires for p in w['prep'])
assert sum(counts.values())==56
assert counts=={'S14':17,'S18':6,'M4_14':8,'M3_14':3,'M2_14':2,'T18':4,'BOX':4,'DATA':4,'J':5,'RJ11':2,'R':1}, counts

# Traverse the conductor graph to prove distribution, separation and independent
# switching. Fixed device contact edges are added only for the requested state.
def net(start, contacts=()):
    graph=defaultdict(set)
    for a,b in [tuple(w['ends']) for w in wires]+INTERNAL_LINKS+list(contacts):
        graph[a].add(b);graph[b].add(a)
    seen={start};todo=[start]
    while todo:
        for neighbor in graph[todo.pop()]-seen:
            seen.add(neighbor);todo.append(neighbor)
    return seen

assert net('BOX:L')=={'BOX:L','F1:LINE'}
assert net('BOX:G')=={'BOX:G','PS1:FG'}
assert net('F1:LOAD')=={'F1:LOAD','PS1:L','W2:COM','K1:1','W1:COM'}
neutral={'BOX:N','PS1:N','K4:A2','K5:A2','K1:A2','K5:3','K1:3'}
assert net('BOX:N')==neutral
assert not occupancy['K1:4'] and not occupancy['K5:4']
assert net('W1:NO')=={'W1:NO','K1:A1'}
assert net('W2:NO')=={'W2:NO','K5:A1'}
assert net('K5:2')=={'K5:2','BOX:SW','K4:A1'}
for master in (False,True):
    for slave in (False,True):
        contacts=[]
        if master: contacts.extend([('K1:1','K1:2'),('K1:3','K1:4'),('W1:COM','W1:NO')])
        if slave: contacts.extend([('K5:1','K5:2'),('K5:3','K5:4'),('W2:COM','W2:NO')])
        live=net('F1:LOAD',contacts)
        assert ('BOX:SW' in live)==(master and slave)
        assert ('K4:A1' in live)==(master and slave)
        assert 'BOX:N' not in live and 'PS1:+V' not in live and 'BOX:G' not in live
        assert ('K1:A1' in live)==master and ('K5:A1' in live)==slave
        returned=net('BOX:N',contacts)
        assert returned==neutral | ({'K1:4'} if master else set()) | ({'K5:4'} if slave else set())
        assert not returned & live and 'BOX:G' not in returned
assert net('PS1:+V')=={'PS1:+V','W2:VCC','W1:VCC'}
assert net('PS1:-V')=={'PS1:-V','W2:GND','W1:GND'}
assert net('JS2')=={'JS2','K4:14','R1:SENSE'}
assert 'JR1' not in net('JS2') and 'K4:11' not in net('JS2')
assert net('JS3')=={'JS3','K4:11'}

# AC branching is explicit at the specified existing screws, with no loose
# splice nodes or hidden links. Only the low-voltage supplies stay simple chains.
assert {frozenset(w['ends']) for w in wires if w['from'] in net('F1:LOAD')}=={
    frozenset(('F1:LOAD',e)) for e in ('PS1:L','W2:COM','K1:1','W1:COM')}
assert {frozenset(w['ends']) for w in wires if w['from'] in neutral}=={
    frozenset(pair) for pair in [('BOX:N','K5:3'),('K5:3','K1:3'),('K5:3','PS1:N'),
                                ('K5:3','K5:A2'),('K1:3','K1:A2'),('K1:3','K4:A2')]}
for route in [
    ['PS1:+V','W2:VCC','W1:VCC'],
    ['PS1:-V','W2:GND','W1:GND'],
]:
    edges={frozenset(w['ends']) for w in wires if w['ends'][0] in net(route[0])}
    assert edges=={frozenset(pair) for pair in zip(route,route[1:])}
    assert all(len(occupancy[node])==(1 if i in (0,len(route)-1) else 2)
               for i,node in enumerate(route))

# Every path reaches its stated terminal/joint, stays orthogonal and uses its
# scheduled insulation color. Shared endpoints display all wire IDs and end codes.
ns={'s':'http://www.w3.org/2000/svg'}
for sheet in ('mains','neutral','switched','dc','returns','signals'):
    svg=ET.parse(ROOT/f'docs/pooldose-{sheet}-wiring.svg').getroot()
    devices=svg.findall('.//s:g[@data-device]',ns)
    if sheet=='signals':
        # The GPIO close-up separates Slave/detector from the Master flow tail.
        assert [d.attrib['data-device'] for d in devices]==['W2','K4','W1']
        r1=svg.find('.//s:g[@data-component="R1"]',ns)
        assert r1 is not None
        assert (r1.attrib['data-from'],r1.attrib['data-to'],r1.attrib['data-ohms'])==('JR1','R1:SENSE','270')
        for board,used in [('W1',{1:'JM1',4:'JM2'}),('W2',{1:'JS2',3:'JR1',4:'JS3'})]:
            leads=svg.findall(f'.//s:g[@data-gpio-board="{board}"]',ns)
            assert [int(g.attrib['data-gpio-pin']) for g in leads]==[1,2,3,4]
            for lead,signal in zip(leads,['GPIO1','GPIO2','3V3','GND']):
                pin=int(lead.attrib['data-gpio-pin'])
                assert lead.attrib['data-signal']==signal
                assert lead.attrib['data-joint']==used.get(pin,'')
                assert lead.attrib['data-unused']==str(pin not in used).lower()
                path=lead.find('s:path[@class="factory-lead"]',ns)
                assert path.attrib['stroke']=={1:'#f4c627',2:'#ed7c19',3:'#d93136',4:'#202832'}[pin]
                assert 'stroke-dasharray' not in path.attrib
                if pin in used:
                    end=tuple(map(float,path.attrib['d'].split(' L')[-1].split(',')))
                    joint=svg.find(f'.//s:g[@data-endpoint="{used[pin]}"]/s:circle',ns)
                    assert end==(float(joint.attrib['cx']),float(joint.attrib['cy']))
    else:
        assert [d.attrib['data-device'] for d in devices]==['W1','K1','W2','K5','K4','PS1','F1']
        xs=[float(d.attrib['data-x']) for d in devices];assert xs==sorted(xs)
    scheduled=[w for w in wires if w['sheet']==sheet]
    paths=svg.findall('.//s:path[@data-from]',ns)
    assert {p.attrib['id'] for p in paths}=={'wire-'+w['id'] for w in scheduled}
    assert len(paths)==len(scheduled)
    for w in scheduled:
        path=svg.find(f".//s:path[@id='wire-{w['id']}']",ns)
        assert [path.attrib['data-from'],path.attrib['data-to']]==w['ends']
        assert path.attrib['stroke']==w['hex']
        pts=[tuple(map(float,p.split(','))) for p in path.attrib['d'][1:].split(' L')]
        assert all(a[0]==b[0] or a[1]==b[1] for a,b in zip(pts,pts[1:])),w['id']
        # A route must not appear to land on or pass through an unrelated screw.
        # Electrical net correctness alone would not catch that drawing error.
        for node in svg.findall('.//s:g[@data-endpoint]',ns):
            if node.attrib['data-endpoint'] in w['ends']: continue
            circle=node.find('s:circle',ns)
            cx,cy=float(circle.attrib['cx']),float(circle.attrib['cy'])
            for a,b in zip(pts,pts[1:]):
                nearest=(min(max(cx,min(a[0],b[0])),max(a[0],b[0])),
                         min(max(cy,min(a[1],b[1])),max(a[1],b[1])))
                distance2=(cx-nearest[0])**2+(cy-nearest[1])**2
                assert distance2>12**2,(w['id'],'passes unrelated terminal',node.attrib['data-endpoint'])
        for i,end in enumerate(w['ends']):
            matches=svg.findall(f".//s:g[@data-endpoint='{end}']",ns)
            assert len(matches)==1,(w['id'],end,len(matches))
            node=matches[-1]
            assert w['id'] in node.attrib['data-wires'].split()
            assert w['prep'][i] in node.attrib['data-prep'].split('/')
            circle=node.find('s:circle',ns)
            assert pts[0 if i==0 else -1]==(float(circle.attrib['cx']),float(circle.attrib['cy'])),(w['id'],end)
    drawn_strips=svg.findall('.//s:g[@data-strip]',ns)
    assert not drawn_strips
    assert not svg.findall('.//s:g[@data-bridge]',ns)
    assert 'follow the end-preparation notes and assembly checkout' in ET.tostring(svg,encoding='unicode')
    for node in data_nodes | fitted_nodes:
        status=SHARED_CHECKS[node].split(':')[0]
        for endpoint in svg.findall(f".//s:g[@data-endpoint='{node}']",ns):
            if endpoint.attrib['data-wires']:
                assert endpoint.attrib['data-status']==status
                assert status in ''.join(endpoint.itertext())
    # Wagos are inside the metal box, not replacements for device terminals.
    assert not any('wago' in ''.join(device.itertext()).lower() for device in devices)

# No obsolete build instructions left behind in maintained entry points.
for name in ('README.md','BOM.md','docs/wiring-schedule.md','docs/build-instructions.html','docs/harness-cut-list.md','docs/termination-schedule.md','docs/physical-wiring.html'):
    text=(ROOT/name).read_text()
    assert not re.search(r'\b[PL]\d{2}\b',text),name
    assert not re.search(r'221-615|REMOVED_|S14\?|S18\?|S10\?|\bD\?',text,re.I),name
    assert not re.search(r'CR14|CR18|XH1|XN1|XCP|34138|34137|UT 4|FBS [23]-6|3044102|3030336|3030242|3200836|3200807',text),name
    assert not re.search(r'revision[ -]6|31 IDs|62 ends|factory bridge',text,re.I),name
assert '128.3' in (ROOT/'BOM.md').read_text()
assert 'HDR-15-5' not in (ROOT/'docs/termination-schedule.md').read_text()
# The combined harness book is generated once; no duplicate legacy copy.
assert not (ROOT/'docs/pooldose-physical-terminal-map.svg').exists()
print('Harness checks passed: 28 wires / 56 ends; PS1 FG ground lead; six shared ferrules; local GPIO1/R1 splice and single detector ends; independent coils, continuous neutral and series pump contacts in all four states.')
