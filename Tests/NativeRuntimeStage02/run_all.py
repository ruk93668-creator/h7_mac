"""Run stage02 gates in order and attach scenario names to actual executable results."""
from pathlib import Path
import subprocess,sys,json
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
specs=[
 ('phase02a','run_arm.py',[
  'NULL通知回调拒绝','统计指针与未初始化保护','重复绑定拒绝','启动不伪造帧进度','拒绝其他Media上下文',
  '轮换失败通知ERROR且不推进序号','轮换成功通知及统计','持有帧正常Stop为BUSY','IN_USE期间丢帧计数','统计临界区恢复PRIMASK']),
 ('phase02a_bsp','run_bsp.py',[
  'NULL BSP回调拒绝','ISR绑定拒绝','初始化后绑定拒绝','采集中绑定拒绝','BSP重复绑定拒绝',
  'BSP错误锁存后通知','重复错误保留首个故障']),
 ('phase02b','run_usb.py',[
  'TEST SOF通知无视频','SOF只通知不在ISR发送','真实完成通知不提前推进偏移','Incomplete取消成功',
  'Incomplete Abort失败保留图像并重试','Alt0通知与释放','Reset通知与释放','Disconnect通知与释放',
  '重复旧DataIn不推进新Token','实际1023B包长整帧完成','25FPS配置对应40个SOF节奏']),
 ('phase02c','run_arm.py',[
  'USB未就绪不请求','任务所有权拒绝','Request Offer并借用','长期借用只持有一帧','Return ACK正常闭环',
  'Media释放失败不ACK且重试','Request队列满保留请求','Offer队列满不重复取帧','Return队列满保留凭据',
  'ACK队列满不重复Release','重复Return只重发ACK','错误lease不释放','错误epoch不释放','旧ACK不清当前借用',
  '非法帧描述仍安全归还','Submit BUSY未借用新帧','Class安全销毁后归还','查询IO不能证明归还',
  '取消未确认时继续持有','取消后确认归还','Stop期间完成待决请求','重新Start后旧epoch无效',
  'lease编号耗尽','epoch耗尽','request编号耗尽','非法Offer凭据拒绝','重复Offer不再次Submit','1000次租借闭环']),
 ('phase02d','run_native.py',[
  'BOOT前不得启动数据面','无Commit Alt1不取帧','完整原生任务视频帧闭环','USB长期持有时Media丢帧不覆盖IN_USE',
  'Control协调正常停止','Abort失败保留原帧和传感器直到安全归还','Reset交错归还','Disconnect交错归还',
  'Alt0交错归还','采集故障先停DMA再DRAINING归还','实际Media归还失败重试','异常Return拒绝',
  '重启epoch拒绝旧Return','控制超时不伪造停止','Sensor Start失败不启动DMA','USB Start失败协调停相机',
  '跨任务命令发送拒绝','读取其他任务通道拒绝','ISR不得运行Camera任务侧处理','真实静态队列满保留租借',
  'Camera静态创建失败','USB静态创建失败','Control静态创建失败','静态队列1失败','静态队列2失败',
  '静态队列3失败','静态队列4失败','静态队列5失败','Initcall失败不启动USB','EOF测量不使用配置FPS',
  '75次SOF合并不累计无限机会','真实任务通知保留Camera事件','33ms模拟采集源下测量实际吞吐和丢帧']),
 ('phase02test','run_native.py',[
  'TEST冷启动不启动Camera','TEST配置完成','100次SOF无视频发送','TEST协调停止','TEST设备描述符',
  'TEST设置地址','TEST断线无相机资源','TEST Reset恢复EP0双方向']),
]
all_rows=[]
for name,runner,names in specs:
    resultfile=HERE/(name+'_results.json');resultfile.unlink(missing_ok=True)
    command=[sys.executable,str(HERE/runner),name,'--count',str(len(names))]
    p=subprocess.run(command,cwd=ROOT,capture_output=True)
    text=(p.stdout+p.stderr).decode('utf-8',errors='replace');(HERE/(name+'_console.log')).write_text(text,encoding='utf-8')
    if p.returncode or not resultfile.exists():print(text);raise SystemExit(p.returncode or 1)
    data=json.loads(resultfile.read_text(encoding='utf-8'));assert len(data['cases'])==len(names)
    for case,title in zip(data['cases'],names):case['name']=title
    data['command']=command
    resultfile.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    all_rows.append({'suite':name,'summary':data['summary'],'command':command})
    print(name,data['summary'],flush=True)
record={'suites':all_rows,'total':sum(x['summary']['total'] for x in all_rows),
        'passed':sum(x['summary']['passed'] for x in all_rows),'failed':sum(x['summary']['failed'] for x in all_rows),'hardware_verified':False}
(HERE/'results.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(record['total'],record['passed'],record['failed'])
