import argparse
from pathlib import Path
import json
import struct
import sys

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path)
parser.add_argument('--deps', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--core', choices=['CM7','CM4'], default='CM7')
args = parser.parse_args()
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, str(args.deps))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

cases = [
    ('完整生命周期', '注册 camera_sim→查找→Init→Start→Stop', '返回0；找到原对象；UNINIT→READY→RUNNING→READY；回调各1次，顺序123'),
    ('注册、查找及重复项', '注册两个设备；查询不同字符串地址的同名设备、缺失名称；重复对象/同名注册', '正确返回原对象或NULL；重复返回-3；链表保持2节点'),
    ('非法参数', 'NULL设备、NULL/空名称及NULL/空查找名称', '操作返回-1；查找返回NULL；无回调、无节点'),
    ('未注册及对象身份', '操作未注册对象；操作与已注册设备同名的另一个对象', 'Init/Start/Stop均返回-2；不调用生命周期回调'),
    ('状态顺序及重启', '未初始化Start/Stop；重复Init/Start/Stop；停止后重新Start/Stop', '非法顺序返回-4；合法回调顺序12323，最终READY'),
    ('缺失生命周期回调', '在对应合法状态下使用NULL lifecycle或缺失init/start/stop', '返回-7；保留原状态；只执行存在的合法回调'),
    ('Init回调失败', '模拟init返回IO=-9', '原样返回-9，进入ERROR，拒绝后续生命周期操作'),
    ('Start回调失败', '模拟start返回BUSY=-5', '原样返回-5，进入ERROR，拒绝后续生命周期操作'),
    ('Stop回调失败', '模拟stop返回TIMEOUT=-8', '原样返回-8，进入ERROR，拒绝后续生命周期操作'),
    ('首次注册状态', '使用READY/RUNNING/ERROR状态进行首次注册', '返回-4，不入链表，不调用回调'),
]
fields = ['failure_line','init_calls','start_calls','stop_calls','callback_trace','list_checks',
          'final_state','register_result','find_same_object','init_result','start_result','stop_result']
with args.image.open('rb') as f:
    elf = ELFFile(f)
    entry = elf.header['e_entry']
    # ARM scatter images store initialized RW data in the flash load segment.
    # Model completed C runtime initialization at the section execution address,
    # since this harness enters Device_TestRun directly rather than __main.
    sections = [(s['sh_addr'],s.data()) for s in elf.iter_sections()
                if s['sh_flags'] & 2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
    symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
rows = []
state_names = ['UNINIT','READY','RUNNING','ERROR']
for case_id,(name,inputs,expected) in enumerate(cases):
    uc = Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    uc.mem_map(0x08000000,0x100000)
    uc.mem_map(0x20000000,0x40000)
    uc.mem_map(0x01000000,0x1000)
    for address,data in sections: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0)
    uc.reg_write(UC_ARM_REG_LR,0x01000001)
    uc.reg_write(UC_ARM_REG_R0,case_id)
    error = None
    try:
        uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
    except Exception as exc:
        error = str(exc)
    values = struct.unpack('<7IiIiii',uc.mem_read(symbols['g_device_test'],48))
    state = dict(zip(fields,values))
    passed = error is None and state['failure_line']==0 and uc.reg_read(UC_ARM_REG_R0)==0 and uc.reg_read(UC_ARM_REG_PC)==0x01000000
    actual = f"最终{state_names[state['final_state']]}; 回调次数Init/Start/Stop={state['init_calls']}/{state['start_calls']}/{state['stop_calls']}; 顺序={state['callback_trace']}; 链表检查={state['list_checks']}"
    if case_id==0:
        actual += f"; 四次操作结果={state['register_result']}/{state['init_result']}/{state['start_result']}/{state['stop_result']}; Find返回原对象={bool(state['find_same_object'])}"
    if passed:
        actual += '; 全部预期断言通过'
    else:
        actual += f"; 失败测试行={state['failure_line']}; 仿真错误={error}"
    row = dict(name=name,input=inputs,expected=expected,actual=actual,result='Pass' if passed else 'Fail',state=state)
    rows.append(row)
    print(f"{case_id+1:02d} {row['result']}: {name}: {actual}")
args.report.mkdir(parents=True,exist_ok=True)
(args.report/'device_results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines = ['# 模拟设备生命周期验收','',
         f'范围：当前{args.core} Device实现；ARMCC5/C99编译，Unicorn ARM仿真，无实际外设。每个用例使用全新仿真状态。','',
         '| 测试 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
lines += [f"| {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',f"结果：{sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass。",'',
          '正常链路：Register→Find→Init→Start→Stop，最终状态为READY，设备仍保持注册。',
          f'测试直接包含实际{args.core} device.c，仅替换生命周期为模拟回调，并提供隔离strcmp桩。',
          '每次链表检查验证头尾关系、节点数量及next/prev双向一致性。',
          '当前实现对任何非零生命周期回调结果都进入ERROR，包括BUSY；这属于观察到的现行策略。',
          '本报告仅描述隔离仿真；Keil Target构建另见构建日志，未进行上板测试。']
(args.report/'device_results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f"Total: {sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass")
raise SystemExit(0 if all(r['result']=='Pass' for r in rows) else 1)
