"""Negative controls prove causal gates reject faulty evidence despite C return=0."""
from pathlib import Path
import importlib.util,copy,json
H=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('audit_evaluate',H/'evaluate.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
rows=[]
def check(name,g,id,mutate):
 a=m.load(g,'baseline','O0',id);b=m.load(g,'candidate','O0',id);mutate(b)
 r=m.evaluate(a,b);assert r['status']=='FAIL';rows.append({'name':name,'status':'PASS','expected_gate':'FAIL','first_rejection':r['first_contract_violation']['name']})
check('ACK body corruption','usb',10,lambda r:next(m for m in r['messages'] if not m['life']).update(bytes=(lambda m:'ff'+m['bytes'][2:])(next(m for m in r['messages'] if not m['life']))))
check('Mode Stop while frame owned','usb',10,lambda r:next(x for x in r['trace'] if x[0]==18).__setitem__(25,1))
check('Reply generation mismatch','usb',10,lambda r:next(x for x in r['messages'] if x['life'] and bytes.fromhex(x['bytes'])[4]==2).update(bytes=(lambda b:(b[:32]+(99).to_bytes(4,'little')+b[36:]).hex())(bytes.fromhex(next(x for x in r['messages'] if x['life'] and bytes.fromhex(x['bytes'])[4]==2)['bytes']))))
check('File byte/hash mismatch','storage',13,lambda r:next(x for x in r['trace'] if x[0]==36).__setitem__(2,12345))
check('Actual lease release in Storage owner','storage',33,lambda r:next(x for x in r['trace'] if x[0]==21).__setitem__(4,4))
check('Debug/status projection mismatch','usb',16,lambda r:next(x for x in r['trace'] if x[0]==12).__setitem__(1,99))
check('FIFO command order changed','storage',12,lambda r:r['command_events'].reverse())
check('Lost assertion observations','storage',13,lambda r:r['checks'].clear())
check('Missing full message observations','usb',98,lambda r:r['messages'].clear())
check('Missing publication evidence','storage',4,lambda r:r['publications'].clear())
a=m.load('usb','baseline','O0',22);b=m.load('usb','candidate','O0',22);assert m.evaluate(a,b)['status']=='PASS';rows.append({'name':'Independent retry original evidence','status':'PASS','expected_gate':'PASS'})
(H/'gate_selftests.json').write_text(json.dumps({'cases':rows,'passed':len(rows)},indent=2));print('Gate selftests PASS',len(rows))
