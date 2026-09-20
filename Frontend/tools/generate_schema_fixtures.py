#!/usr/bin/env python3
"""Authored pure manifest corpus; expectations come from unchanged store.validate.
No temporary stores, game assets, binary queries, or filesystem snapshot reads.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lodge import store, managed_state, native_session
from lodge.genesis_hunt import SCORE_MODIFIERS

H,I,A,G0,G1,G2,S1,S2=[f'00000000-0000-0000-0000-{i:012x}' for i in range(1,9)]
D='a'*64

def base(version=1,generations=0):
    r={'algorithm':'huntdat-sha256-v1','sha256':D,'file_count':1,'byte_count':8}
    a={'id':A,'hunter_id':H,'instance_id':I,'ownership':'managed','state_key':'slot:0','filename_slot':0,'origin':'personal','writable':False,'authority':'independent-snapshot','revision':copy.deepcopy(r),'files':[{'path':'trophy00.sav','sha256':D,'size':8,'kind':'sav'}]}
    d={'schema_version':version,'hunters':{H:{'id':H,'name':'Author'}},'active_hunter':H,'instances':{I:{'id':I,'mode':'registered','path_flavor':'posix','path':'/authored/install','dialect_hint':'unknown','revision':copy.deepcopy(r),'revisions':[r],'engine_evidence':[]}},'associations':{A:a},'host_settings':{}}
    if version==2:
        d['state_upgrade']={'from_version':1,'backup':managed_state.UPGRADE_BACKUP,'at':'','backup_sha256':D}
        a['authority']=managed_state.AUTHORITY
        gs={G0:{'id':G0,'sequence':0,'predecessor':None,'source_session':None,'kind':'original-import','created_at':'2000-01-01T00:00:00Z','members':managed_state.members_from_import(a),'provenance':managed_state.provenance(a),'snapshot':f'snapshots/{A}'}}
        rs={};head=G0
        for sequence,gid,sid in [(1,G1,S1),(2,G2,S2)][:generations]:
            spec={'kind':'managed-native-hunt-v1','executable':{'path':'historical-engine','sha256':D},'contract':copy.deepcopy(native_session.CAPABILITY),'shell':False,'timeout_seconds':30,'config_sha256':hashlib.sha256(native_session.CONFIG).hexdigest(),'cwd':'historical-relative-dir','argv':['historical-engine','--session-contract=1','--session-slot=0','--session-root=x','--session-source=y','--session-baseline=z','prj=huntdat/areas/area1','din=1','wep=1','dtm=0',SCORE_MODIFIERS]}
            g={'id':gid,'sequence':sequence,'predecessor':head,'source_session':sid,'kind':'accepted-native-hunt','created_at':f'2000-01-0{sequence+1}T00:00:00Z','members':managed_state.members_from_import(a),'provenance':managed_state.provenance(a),'snapshot':f'generations/{A}/{gid}','policy':'genesis-current-mee-hunt-v1','execution':spec,'revision':copy.deepcopy(r)}
            receipt={'schema_version':1,'association_id':A,'session_id':sid,'generation_id':gid,'predecessor':head,'members':copy.deepcopy(g['members']),'accepted_at':g['created_at'],'policy':g['policy'],'execution':copy.deepcopy(spec),'revision':copy.deepcopy(r),'candidate_sha256':D,'acceptance':'explicit'}
            gs[gid]=g;rs[sid]=receipt;head=gid
        a['managed_state']={'schema_version':1,'current_generation':head,'generations':gs,'receipts':rs}
    return d

def outcome(raw):
    result={}
    for host,path_type in [('posix',PurePosixPath),('nt',PureWindowsPath)]:
        # Only the host's pure Path parsing is substituted. Validation functions
        # and JSON decoding remain the unchanged authoritative implementation.
        with patch.object(store,'Path',path_type),patch.object(managed_state,'Path',path_type):
            try:store.validate(json.loads(raw,object_pairs_hook=store._unique_object));result[host]='valid'
            except (ValueError,KeyError,TypeError,AttributeError):result[host]='invalid'
    return result

def corpus():
    result=[]
    def add(name,d):
        raw=d if isinstance(d,str) else json.dumps(d,ensure_ascii=True,separators=(',',':'))
        result.append({'name':name,'json':raw,'outcome':outcome(raw)})
    for ver,n in [(1,0),(2,0),(2,1),(2,2),(1,'engine')]:
        d=base(ver,n)
        if n=='engine':
            evidence=[{'path':'hunt.exe','sha256':D,'semantics':'unknown','extra':{'value':1}}]
            d['instances'][I]['engine_evidence']=copy.deepcopy(evidence)
            d['instances'][I]['engine_relocation_reviews']=[{'status':'required','observed_at':'2000-01-01','from':{'path_flavor':'posix','path':'/old'},'to':{'path_flavor':'nt','path':'C:/new'},'baseline_engine_evidence':copy.deepcopy(evidence),'destination_engine_evidence':[{'path':'new.exe','sha256':'b'*64,'semantics':'unknown'}]}]
        add(f'base-{ver}-{n}',d)
        # All nested required fields and exact type distinctions are exercised
        # by invoking the real Python validator, not a second rules mirror.
        def paths(value,p=()):
            if isinstance(value,dict):
                for k,v in value.items():yield p+(k,);yield from paths(v,p+(k,))
            elif isinstance(value,list):
                for k,v in enumerate(value):yield p+(k,);yield from paths(v,p+(k,))
        for index,p in enumerate(paths(d)):
            for j,value in enumerate([None,False,True,0,1,1.0,-1,[],{},'', 'unknown',2**80]):
                c=copy.deepcopy(d);at=c
                for k in p[:-1]:at=at[k]
                at[p[-1]]=value;add(f'mutate-{ver}-{n}-{index}-{j}',c)
            if isinstance(p[-1],str):
                c=copy.deepcopy(d);at=c
                for k in p[:-1]:at=at[k]
                del at[p[-1]];add(f'delete-{ver}-{n}-{index}',c)
    for flavor,paths in [('posix',['/','//','///a','/a/../b','/a/./b','relative','/a\x00b']),('nt',['C:/','c:/a','C:a','/a','\\a','//server/share','//server/share/a','//?/UNC/server/share','//?/C:/a','//./device','//server','///a/b','//server//a','1:/a'])]:
        for path in paths:
            d=base();d['instances'][I].update(path_flavor=flavor,path=path);add(f'locator-{flavor}-{path!r}',d)
    for root,child in [('C:/ΟΣ','c:/ος/x'),('C:/ΟΣ','c:/οσ/x'),('C:/a','c:/a/./b'),('//server/share','//SERVER/share/a'),('/a','/a/b'),('//a','/a/b')]:
        d=base();i=d['instances'][I];flavor='nt' if ':' in root or root.startswith('//server') else 'posix';i.update(mode='managed',path_flavor=flavor,path=child,managed_root={'path_flavor':flavor,'path':root});add(f'containment-{root}-{child}',d)
    for name in ['', '\x1c','\u200b','\u0085','\u00a0','x']:
        d=base();d['hunters'][H]['name']=name;add('name-'+repr(name),d)
    for first,second in [('ß','ss'),('İ','i\u0307'),('Σ','ς'),('𐐀','𐐨'),('a','A'),('./a','a')]:
        d=base();a=d['associations'][A];a['files']=[copy.deepcopy(a['files'][0]),copy.deepcopy(a['files'][0])];a['files'][0]['path']=first;a['files'][1]['path']=second;add('fold-'+first+'-'+second,d);assert a['files'][0]['path']==first and a['files'][1]['path']==second;assert set(result[-1]['outcome'].values())=={('invalid' if first.casefold()==second.casefold() else 'valid')}
    for member in ['.', './', 'a//b','a/./b','a/../b','a\x00b','/a','a\\b','a:b']:
        for v in [1,2]:
            d=base(v);a=d['associations'][A];a['files'][0]['path']=member
            if v==2:a['managed_state']['generations'][G0]['members']=managed_state.members_from_import(a)
            add(f'member-{v}-{member!r}',d)
            if member=='/a':assert result[-1]['outcome']=={'posix':'invalid','nt':'valid'}
    for version in [1,2]:
        d=base(version);a=d['associations'][A];a['ownership']='referenced';a['authority']='native-files';add(f'referenced-v{version}',d)
    for spelling in ['/authored/install','/authored/./install','/authored//install']:
        d=base();other='00000000-0000-0000-0000-000000000009';i=copy.deepcopy(d['instances'][I]);i.update(id=other,path=spelling);d['instances'][other]=i;add('raw-locator-'+spelling,d)
    d=base(2,1);other='00000000-0000-0000-0000-000000000009';a=copy.deepcopy(d['associations'][A]);a['id']=other
    for g in a['managed_state']['generations'].values():
        g['provenance']['id']=other;g['snapshot']=g['snapshot'].replace(A,other)
    for r in a['managed_state']['receipts'].values():r['association_id']=other
    d['associations'][other]=a;add('cross-association-session',d)
    for paths in [('engine.exe','engine.exe'),('engine.exe','ENGINE.EXE')]:
        d=base();d['instances'][I]['engine_evidence']=[{'path':p,'sha256':D,'semantics':'unknown'} for p in paths];add('engine-raw-'+repr(paths),d)
        assert set(result[-1]['outcome'].values())=={('invalid' if paths[0]==paths[1] else 'valid')}
    for state_key in ['slot:0','slot:1']:
        d=base();a=d['associations'][A];a.update(ownership='referenced',authority='native-files');other='00000000-0000-0000-0000-000000000009';b=copy.deepcopy(a);b.update(id=other,state_key=state_key);d['associations'][other]=b;add('referenced-key-'+state_key,d)
        assert set(result[-1]['outcome'].values())=={('invalid' if state_key=='slot:0' else 'valid')}
    variants=[('timeout_seconds',None,x) for x in [29,30.0,3600,3600.0,3601,True,float('nan'),float('inf')]]
    variants += [('argv',i,x) for i,values in [(6,['prj=huntdat/areas/external','prj=huntdat/areas/area8','prj=huntdat/areas/area0','prj=huntdat/areas/area9']),(7,['din=256','din=512','din=3']),(8,['wep=128','wep=256']),(9,['dtm=2','dtm=3'])] for x in values]
    for field,index,value in variants:
        d=base(2,1);h=d['associations'][A]['managed_state']
        for spec in [h['generations'][G1]['execution'],h['receipts'][S1]['execution']]:
            if index is None:spec[field]=value
            else:spec[field][index]=value
        add('execution-coordinated-'+repr((field,index,value)),d)
    d=base(2,1);a=d['associations'][A];a['filename_slot']=2**80
    for g in a['managed_state']['generations'].values():g['provenance']['filename_slot']=2**80
    a['managed_state']['generations'][G1]['execution']['argv'][2]=f'--session-slot={2**80}'
    a['managed_state']['receipts'][S1]['execution']['argv'][2]=f'--session-slot={2**80}'
    add('historical-large-slot',d);assert set(result[-1]['outcome'].values())=={'valid'}
    for value in [None,False,[],{},0,0.0,'',float('nan'),float('inf'),2**150]:
        d=base(2,2);d['unknown']={'value':value};add('unknown-'+repr(value),d)
        d=base();d['instances'][I]['revision']['extra']=value;d['instances'][I]['revisions'][0]['extra']=value;add('revision-extra-'+repr(value),d)
        d=base();d['hunters'][H]['archived_at']=value;add('archived-'+repr(value),d)
    for current,historical in [(1.0,1),(True,1),(2**53+1,float(2**53)),(2**100,float(2**100))]:
        d=base();d['instances'][I]['revision']['file_count']=current;d['instances'][I]['revisions'][0]['file_count']=historical;add('numeric-'+repr(current)+'-'+repr(historical),d)
    d=base(2,2);d['associations'][A]['managed_state']['generations'][G0]['predecessor']=G2;add('cycle',d)
    d=base(2,2);d['associations'][A]['managed_state']['current_generation']=G1;add('disconnected',d)
    d=base(2);d['associations'][A]['managed_state']['generations'][G0]['members'][0]['extra']=None;add('extra-g0-member',d)
    d=base(2);d['associations'][A]['managed_state']['generations'][G0]['provenance']['extra']=None;add('extra-provenance',d)
    d=base();d['state_upgrade']={'reserved':'unknown'};d['associations'][A]['managed_state']={'reserved':'unknown'};add('reserved-v1-retained',d)
    add('duplicate','{"schema_version":1,"schema_version":1}')
    add('escaped-duplicate','{"schema_version":1,"\\u0073chema_version":1}')
    add('nested-duplicate','{"unknown":{"x":1,"x":2}}')
    # Store exact JSON inputs as a prefix/middle/suffix delta against the
    # nearest authored base. This keeps thousands of one-field cases reviewable
    # without committing tens of megabytes of repeated complete histories.
    bases=[json.dumps(base(v,n),ensure_ascii=True,separators=(',',':'))
           for v,n in [(1,0),(2,0),(2,1),(2,2)]]
    for case in result:
        raw=case.pop('json');choices=[]
        for index,template in enumerate(bases):
            prefix=0
            while prefix<min(len(raw),len(template)) and raw[prefix]==template[prefix]:prefix+=1
            suffix=0
            while suffix<min(len(raw),len(template))-prefix and raw[-suffix-1]==template[-suffix-1]:suffix+=1
            middle=raw[prefix:len(raw)-suffix if suffix else len(raw)]
            choices.append((len(middle),index,prefix,suffix,middle))
        _,index,prefix,suffix,middle=min(choices)
        case['input']=[index,prefix,suffix,middle]
    return {'bases':bases,'cases':result}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args()
    target=Path(__file__).resolve().parents[1]/'tests/compatibility/schema.json'
    data=json.dumps(corpus(),ensure_ascii=True,indent=2)+'\n'
    if args.check:
        if target.read_text()!=data:raise SystemExit('schema fixtures differ from Python authority')
    else:target.write_text(data)
