"""Python-authoritative native pure-schema differential verification."""
import json
import os
from pathlib import Path
import random
import subprocess
import sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
import c2_reference_paths  # noqa: E402
exe=sys.argv[1]
corpus=json.loads((root/'tests/compatibility/schema.json').read_text())
cases=corpus['cases']
for case in cases:
    index,prefix,suffix,middle=case['input'];base=corpus['bases'][index]
    case['json']=base[:prefix]+middle+(base[-suffix:] if suffix else '')
batch=subprocess.run([exe,'--batch'],input=('\n'.join(c['json'] for c in cases)+'\n').encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
lines=batch.stdout.splitlines()
assert len(lines)==len(cases),(len(lines),len(cases),batch.stderr)
for c,line in zip(cases,lines):
    fields=line.split(b'\t',1);actual=fields[0].decode(errors='replace')
    assert actual==c['outcome'][os.name],(c['name'],c['outcome'],line)
    if actual=='valid':
        expected=json.dumps(json.loads(c['json']),ensure_ascii=True,separators=(',',':')).encode()
        assert fields[1]==expected,c['name']
print(f'{len(cases)} authored schema cases passed')
values=[None,False,True,0,1,1.0,-0.0,2**53,2**53+1,float(2**53),2**100,float(2**100),2**1024,float('inf'),float('nan'),'a',[float('nan')],{'x':float('nan')},{'x':1,'y':[True]},{'y':[1.0],'x':True}]
for a in values:
    for b in values:
        raw=json.dumps([a,b]);x,y=json.loads(raw)
        p=subprocess.run([exe,'--equal-stdin'],input=raw.encode(),stdout=subprocess.PIPE,check=True)
        assert p.stdout==(b'true' if x==y else b'false'),(a,b,p.stdout)
rng=random.Random(391)
strings=['ΟΣ','ΟΣΑ','AΣ\u0301','AΣ\u0301A','AΣ\u0345','AΣ\u0345A','İ','ß','\ud800']
strings+=[''.join(chr(rng.randrange(0x110000)) for _ in range(20)) for _ in range(100)]
for s in strings:
    for mode,fn in [('--lower-stdin',str.lower),('--fold-stdin',str.casefold)]:
        p=subprocess.run([exe,mode],input=json.dumps(s).encode(),stdout=subprocess.PIPE,check=True)
        assert json.loads(p.stdout)==fn(s),(mode,repr(s),p.stdout)
print('400 semantic equality and Unicode case probes passed')
# Byte protocol must distinguish schema failure, malformed UTF-8, and resource
# exhaustion, with controlled process exits rather than crash-as-rejection.
for raw,code,prefix in [(b'\xff',1,b'invalid\n'),(b'['*1002,3,b'resource\n')]:
    p=subprocess.run([exe,'--manifest-stdin'],input=raw,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert p.returncode==code and p.stdout.startswith(prefix),(p.returncode,p.stdout,p.stderr)

# Native v2-only provenance validation. The historical corpus/reference remains
# unchanged; older implementations must reject v2 instead of broadening v1.
import copy
base = next(json.loads(c['json']) for c in cases if c['outcome'][os.name] == 'valid'
            and any(a.get('managed_state', {}).get('receipts')
                    for a in json.loads(c['json']).get('associations', {}).values()))
expanded = copy.deepcopy(base)
a = next(a for a in expanded['associations'].values() if a.get('managed_state', {}).get('receipts'))
h = a['managed_state']
r = next(iter(h['receipts'].values()))
g = h['generations'][r['generation_id']]
for record in (r, g):
    record['policy'] = 'genesis-current-mee-hunt-v2'
    record['execution']['argv'][7:9] = ['din=511', 'wep=255']
    record['execution']['argv'][11:] = ['-camo', '-radar', '-scent', '-double']

def expanded_valid(data):
    result = subprocess.run([exe, '--batch'], input=(json.dumps(data)+'\n').encode(),
                            stdout=subprocess.PIPE, check=True).stdout
    return result.startswith(b'valid\t')

assert expanded_valid(expanded)
for bad_policy, mask_index, bad_mask, flags in [
    ('genesis-current-mee-hunt-v1', None, None, None),
    ('genesis-current-mee-hunt-v3', None, None, None),
    (None, 7, 'din=512', None), (None, 8, 'wep=256', None),
    (None, 7, 'din=0', None), (None, 8, 'wep=0', None),
    (None, 7, 'din=03', None), (None, 8, 'wep=-1', None),
    (None, None, None, ['-nightvision']), (None, None, None, ['-tranq']),
    (None, None, None, ['-observ']), (None, None, None, ['-camo', '-camo']),
    (None, None, None, ['-radar', '-camo'])]:
    changed = copy.deepcopy(expanded)
    h = next(a['managed_state'] for a in changed['associations'].values() if a.get('managed_state', {}).get('receipts'))
    receipt = next(iter(h['receipts'].values()))
    for record in (receipt, h['generations'][receipt['generation_id']]):
        if bad_policy: record['policy'] = bad_policy
        if mask_index is not None: record['execution']['argv'][mask_index] = bad_mask
        if flags is not None: record['execution']['argv'][11:] = flags
    assert not expanded_valid(changed), (bad_policy, bad_mask, flags)
print('Native v2 receipt masks/flags validated; v1 authorization remains bounded')
