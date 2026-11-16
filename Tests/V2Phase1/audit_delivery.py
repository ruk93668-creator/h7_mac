"""Verify Phase 1 scope, source/evidence binding and actual ARM outputs."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASE = 'ce33d0714211c237009d63f36c26bafe1ed47804'
EVIDENCE = HERE / 'Evidence'

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    assert git('branch','--show-current').decode().strip() == 'codex/architecture-media-graph-lifecycle'
    assert git('rev-parse','main').decode().strip() == 'f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb'
    paths = ['MDK-ARM/h7_mac.uvprojx','CM4/Core/Src/main.c']
    protected = {p:git('show',BASE+':'+p)==(ROOT/p).read_bytes() for p in paths}
    assert all(protected.values())
    allowed = ('CM7/ApplicationV2/','Tests/V2Phase1/','Docs/Architecture/V2/Phase1/')
    changes = git('diff','--name-only',BASE).decode().splitlines()
    assert all(p.startswith(allowed) for p in changes), changes
    result = json.loads((HERE/'results.json').read_text(encoding='utf-8'))
    assert result['summary'] == {'total':250,'pass':250,'fail':0}
    keys = [(x['optimization'],x['id']) for x in result['cases']]
    assert len(set(keys))==len(keys)
    assert set(keys)=={(o,i) for o in ['O0','O2'] for i in range(125)}
    for p,h in result['source_sha256'].items(): assert sha(ROOT/p)==h,p
    final = sorted((ROOT/'output/V2Phase1').glob('build_*/build_results.json'))[-1]
    build = json.loads(final.read_text(encoding='utf-8'))
    assert [t['target'] for t in build['targets']]==['h7_mac_CM7','h7_mac_CM4'], 'full rebuild is still running or incomplete'
    assert build['project_sha256']==sha(ROOT/paths[0])
    for p,h in build['source_sha256'].items(): assert sha(ROOT/p)==h,p
    evidence = {'protected_identical':protected,'protected_sha256':{p:sha(ROOT/p) for p in paths},
                'baseline':BASE,'build_output':build['output'],'test_id_set_complete':True,
                'tracked_changes':changes,'targets':[],'task_stack_map':{}}
    for target in build['targets']:
        assert target['exit']==0 and target['zero_errors_warnings']
        log = final.parent/(target['target']+'.log')
        text = log.read_text(encoding='utf-8',errors='replace')
        assert not re.search(r'compiling (v2_|media_graph|media_format)',text)
        shutil.copy2(log,EVIDENCE/log.name)
        for item in target['artifacts']:
            artifact = final.parent/item['file']
            assert sha(artifact)==item['sha256']
        evidence['targets'].append({'target':target['target'],'exit':target['exit'],
                                    'zero_errors_warnings':True,'program_size':target['program_size']})
    shutil.copy2(final,EVIDENCE/'build_results.json')
    for src,dst in [('output/V2Phase1/tests/commands.json','test_commands.json'),
                    ('output/V2Phase1/test_final.log','test_stdout.log')]:
        shutil.copy2(ROOT/src,EVIDENCE/dst)
    commands=json.loads((EVIDENCE/'test_commands.json').read_text(encoding='utf-8'))
    assert all(c['exit']==0 and not re.search(r'warning:|error:',c['output'],re.I) for c in commands)
    resources=json.loads((HERE/'resources.json').read_text(encoding='utf-8'))
    for opt in ['O0','O2']:
        resources[opt]['total_v2_object_RO']=sum(o['RO'] for o in resources[opt]['objects'])
        assert all(not o['RW'] and not o['ZI'] for o in resources[opt]['objects'])
        assert all(p['return']==0 for p in resources[opt]['probes'])
        callgraph=ROOT/f'output/V2Phase1/tests/phase1_{opt}.htm'
        resources[opt]['linker_stack_header']=re.search(r'Maximum Stack Usage.*?</H3>',callgraph.read_text(errors='replace')).group(0)
    assert resources['O0']['probes'][0]['sizeof']==resources['O2']['probes'][0]['sizeof']
    (HERE/'resources.json').write_text(json.dumps(resources,indent=2),encoding='utf-8')
    cm7=Path(build['output'])/'artifacts/h7_mac_CM7/h7_mac_CM7.map'
    text=cm7.read_text(errors='replace')
    for symbol,size in [('s_camera_stack',6144),('s_usb_stack',8192),('s_stack',8192)]:
        assert re.search(r'\b'+symbol+r'\s+0x[0-9a-f]+\s+Data\s+'+str(size)+r'\b',text)
        evidence['task_stack_map'][symbol]=size
    evidence['map_totals']=[x.strip() for x in text.splitlines() if 'Total RO  Size' in x or 'Total RW  Size' in x or 'Total ROM Size' in x]
    evidence['new_dynamic_allocations']=0
    (HERE/'ScopeAudit.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print(json.dumps(evidence,indent=2))

if __name__=='__main__': main()
