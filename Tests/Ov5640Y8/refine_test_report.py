from pathlib import Path
p=Path('Tests/Ov5640Y8/run_tests.py');s=p.read_text(encoding='utf-8');s=s.replace("'count','passed','failed','done'","'writes','reserved1','reserved2','reserved3'")
a=s.index(' actual=f');b=s.index('\n print(',a)
s=s[:a]+''' actual=f"寄存器写入{state['writes']}次，SCCB字节{state['tx_count']}，Timer堆={state['heap']}，首次Initcall返回{state['init_result']}，断言{state['checks']}项"
 expected='返回预期错误，保持边界和总线释放条件' if id not in [0,24,28,29,32,33,38] else '完成对应操作；配置成功时160×120、Y8和彩条关键寄存器一致，主循环不重复配置'
 observed={hex(reg):u.mem_read(syms['registers']+reg,1)[0] for reg in [0x3808,0x3809,0x380A,0x380B,0x4300,0x501F,0x503D,0x3008,0x3037]}
 rows.append({'name':name,'input':name,'expected':expected,'result':'PASS' if passed else 'FAIL','actual':actual,'registers':observed,'state':state,'error':error,'sccb_tx_prefix':bytes(u.mem_read(syms['g_wire_tx'],min(128,state['tx_count']))).hex(' ')})'''+s[b:]
s=s.replace("'| 测试 | 实际结果 | 判定 |','|---|---|---|'","'| 测试输入 | 预期 | 实际结果 | 判定 |','|---|---|---|---|'")
s=s.replace("{r['name']} | {r['actual']}","{r['name']} | {r['expected']} | {r['actual']}")
p.write_text(s,encoding='utf-8')
