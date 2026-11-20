"""Explicit PIO USB MMIO model. Never writes software ownership/proof fields."""
import struct
from unicorn import *
from unicorn.arm_const import *
BASE=0x40080000
GINT=BASE+0x14; MASK=BASE+0x18; DCTL=BASE+0x804; DAINT=BASE+0x818; DMASK=BASE+0x81c; OMASK=BASE+0x814; IMASK=BASE+0x810
class VM:
 def __init__(self,symbols,sections,entry,observations):
  self.symbols=symbols;self.entry=entry;self.observations=observations
  self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(BASE,0x40000),(0xe0000000,0x100000)]:self.cpu.mem_map(a,n)
  for a,d in sections:self.cpu.mem_write(a,d)
  self.q=[];self.payload=[];self.pending=[];self.reads=[];self.writes=[];self.trace=[]
  self.auto_nak=True;self.hold_disable=set();self.nak_requested=False;self.rx_flushes=0
  self.disable_due={};self.hold_release=False;self.inject_reset_on_data=False;self.inject_reset_on_terminal=False
  self.cpu.hook_add(UC_HOOK_MEM_READ,self.read,begin=BASE,end=BASE+0x3ffff)
  self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=BASE,end=BASE+0x3ffff)
  self.cpu.hook_add(UC_HOOK_CODE,self.code)
 def rd(self,a):return struct.unpack('<I',self.cpu.mem_read(a,4))[0]
 def wr(self,a,v):self.cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
 def queue(self,ep,data=b'',kind=2):
  pad=data+b'\0'*((-len(data))%4)
  self.q.append((ep|(len(data)<<4)|(kind<<17),list(struct.unpack('<'+'I'*(len(pad)//4),pad))))
 def levels(self):
  out=sum((1<<e) for e in range(9) if self.rd(BASE+0xb08+e*32)&self.rd(OMASK))
  ins=sum((1<<e) for e in range(9) if self.rd(BASE+0x908+e*32)&self.rd(IMASK))
  self.wr(DAINT,(out<<16)|ins)
  v=self.rd(GINT)&~((1<<4)|(1<<18)|(1<<19))
  if self.q:v|=1<<4
  if out&(self.rd(DMASK)>>16):v|=1<<19
  if ins&self.rd(DMASK):v|=1<<18
  self.wr(GINT,v)
 def read(self,cpu,access,a,size,value,ctx):
  if a in (GINT,DAINT):self.levels()
  if a==BASE+0x1c:self.wr(a,self.q[0][0] if self.q else 0)
  if a==BASE+0x20:
   if not self.q or self.payload:raise RuntimeError('invalid destructive status pop')
   status,words=self.q.pop(0);self.payload.extend(words);self.wr(a,status);self.reads.append(dict(kind='pop',status=hex(status)))
   if (status>>17)&15==1:self.wr(GINT,self.rd(GINT)|(1<<7));self.wr(DCTL,self.rd(DCTL)|8)
   self.levels()
  if a==BASE+0x1000:
   if not self.payload:raise RuntimeError('payload FIFO underrun')
   self.wr(a,self.payload.pop(0));self.reads.append(dict(kind='data'))
   if self.inject_reset_on_data:self.wr(GINT,self.rd(GINT)|(1<<12));self.inject_reset_on_data=False
  if a==BASE+0x10:self.wr(a,(self.rd(a)|(1<<31))&~((1<<5)|(1<<4)))
  for ep in range(9):
   if a==BASE+0xb00+ep*32 and ep not in self.hold_disable and self.rd(a)&(1<<30) and self.rd(a)&(1<<31):
    self.wr(a,self.rd(a)&~((1<<31)|(1<<30)));self.wr(a+8,self.rd(a+8)|2)
 def write(self,cpu,access,a,size,value,ctx):
  self.writes.append(dict(address=hex(a),value=hex(value),pc=hex(cpu.reg_read(UC_ARM_REG_PC))))
  if self.inject_reset_on_terminal and a==BASE+0xb48 and value&1:
   self.wr(GINT,self.rd(GINT)|(1<<12));self.inject_reset_on_terminal=False
  if a in (GINT,BASE+0x04) or any(a==BASE+off+e*32 for off in (0x908,0xb08) for e in range(9)):
   self.pending.append((a,self.rd(a)&~value))
  if a==DCTL:
   v=(value&~((1<<9)|(1<<10)|8))|(self.rd(a)&8)
   if value&(1<<10) and not self.hold_release:v&=~8;self.nak_requested=False;self.pending.append((GINT,self.rd(GINT)&~(1<<7)))
   if value&(1<<9) and not self.nak_requested:
    self.nak_requested=True
    if self.auto_nak:self.queue(0,kind=1)
   self.pending.append((a,v))
  for ep in range(9):
   if a==BASE+0xb00+ep*32 and value&(1<<30) and value&(1<<31):self.disable_due[ep]=4
  if a==BASE+0x10 and value&(1<<4):self.rx_flushes+=1
 def code(self,cpu,a,size,ctx):
  while self.pending:
   x,v=self.pending.pop(0);self.wr(x,v)
  for ep in list(self.disable_due):
   self.disable_due[ep]-=1
   if self.disable_due[ep]<=0 and ep not in self.hold_disable:
    a=BASE+0xb00+ep*32;self.wr(a,self.rd(a)&~((1<<31)|(1<<30)));self.wr(a+8,self.rd(a+8)|2);del self.disable_due[ep]
 def event(self,ep,bits):self.wr(BASE+0xb08+ep*32,self.rd(BASE+0xb08+ep*32)|bits)
 def complete(self,ep):self.wr(BASE+0xb00+ep*32,self.rd(BASE+0xb00+ep*32)&~(1<<31));self.event(ep,1)
 def step(self,op,arg=0):
  c=self.cpu;c.reg_write(UC_ARM_REG_SP,0x2003fff0);c.reg_write(UC_ARM_REG_LR,0x01000001);c.reg_write(UC_ARM_REG_R0,op);c.reg_write(UC_ARM_REG_R1,arg)
  c.emu_start(self.symbols[self.entry]|1,0x01000000,count=2000000,timeout=4000000)
  assert c.reg_read(UC_ARM_REG_PC)==0x01000000,'execution did not return'
  out={}
  for symbol,fields in self.observations.items():out[symbol]=dict(zip(fields,struct.unpack('<'+'I'*len(fields),c.mem_read(self.symbols[symbol],len(fields)*4))))
  self.trace.append(dict(op=op,observations=out,registers=dict(DCTL=self.rd(DCTL),GINTSTS=self.rd(GINT),DOEPCTL2=self.rd(BASE+0xb40),DOEPINT2=self.rd(BASE+0xb48)),primask=c.reg_read(UC_ARM_REG_PRIMASK)))
  return out
