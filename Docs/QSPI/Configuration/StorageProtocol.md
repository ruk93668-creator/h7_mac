# 配置双Slot线协议与掉电模型（设计建议）

输入与业务范围见 [Design](Design.md)。所有地址和格式为拟议方案，尚未编程或实板验证。

## 分区和精确布局

Slot A、B各独立4096B擦除扇区，用符号 `CONFIG_SLOT_A_BASE`、`CONFIG_SLOT_B_BASE`。要求4KiB对齐、互不重叠、落在16MiB内且完整分配产品独占。两者不要求物理相邻；不沿用H1 `0x00FFF000` 或其相邻 `0x00FFE000` 为产品授权。每Slot只存一条记录，不实现同扇区日志。

|相对偏移|长度|字段/规定|
|---|---|---|
|0x000|4|Magic ASCII `RCFG`，字节52 43 46 47|
|0x004|2|Schema Version，uint16 LE=1|
|0x006|2|Header Length，uint16 LE=64|
|0x008|4|Sequence，uint32 LE，初次为0|
|0x00C|4|Payload Length，uint32 LE=16|
|0x010|4|Record Kind，uint32 LE=1（录像配置）|
|0x014|4|Flags=0，不识别置位即拒绝|
|0x018|4|Reserved=0|
|0x01C|4|CRC32，uint32 LE|
|0x020..03F|32|保留为FF，解析时验证|
|0x040..04F|16|Payload四个uint32 LE：partition/fps/bytes/ms|
|0x050..0FF|176|保持FF，不写|
|0x100..10F|16|Commit Record，独立物理页：ASCII `CMIT` + Sequence + CRC32 + bitwise_not(CRC32)，各4B|
|0x110..FFF|3824|保持FF，不写|

CRC采用CRC-32/ISO-HDLC：反射多项式0xEDB88320，初值/最终异或0xFFFFFFFF，RefIn/RefOut=true，`123456789`校验0xCBF43926。覆盖偏移0..79共80B，计算时0x01C..01F视为00；包括Header、序号、长度、保留及全部Payload，排除Commit及尾部FF。不能只对Payload算CRC，否则损坏序号可能选择错副本。

写入分两次BSP Program：先80B body（第0页），验读；再16B Commit（第1页），验读。擦除后这两段分别仍为FF，符合现Program“完整请求必须已擦除”前置条件。不在同页回写有效位，不在已有Header上修改CRC，不假设16B编程原子。Schema1规定尾部全FF，加载需完整读取4096B检查；未知Schema只做有界基本识别，不按其长度访问任意内存。

## 加载和副本选择

1. QSPI初始状态及完整两份读取成功后才进行选择。每次4096B读取须确认completed=4096且BSP成功；任一I/O失败不得当“空白”。保存禁用，不向读错Slot擦写。
2. 全FF为EMPTY；非空Slot必须满足Magic/结构/Commit四字段精确匹配、CRC和业务合法性，才为VALID。未提交、部分提交、错误CRC、超界长度等均INVALID，保留具体原因。
3. Schema不支持但识别到RCFG标识：UNSUPPORTED，保存禁用，防止旧固件抹除新版配置。此时采用固件默认并上报降级，保留两份字节供审计；不自动迁移，不靠“认识的旧副本”偷偷解除保护。
4. 仅一份VALID且另一EMPTY/INVALID：选VALID，标记DEGRADED。允许以后显式保存到非有效Slot，前提是该Slot归属、备份及所有写准入已解决；启动不自动修复。
5. 两份VALID：令 `d=(uint32_t)(seqA-seqB)`。d=0且80B body完全相同，视为同代冗余，确定选择A；d=0但内容不同为CONFLICT。d=0x80000000为AMBIGUOUS。0<d<0x80000000选A；其余选B。冲突/歧义用默认且禁保存，不任意挑一份。
6. 均无VALID：EMPTY/INVALID组合按诊断原因用默认，绝不启动时自动格式化。若两个Slot均在已正式分配、内容处置与备份授权内，可以在后续显式首次Commit时从A序号0建立；UNKNOWN归属、UNSUPPORTED或I/O错误禁止覆盖。

正常每次成功代次只+1（包括0xFFFFFFFF→0），每次只擦旧代目标，两份相差1，因此半圈比较成立。半圈或同代冲突可能来自离线写入、损坏或协议违例，不能宣称永不发生。有效性先于新旧比较，不能按一个CRC坏的高序号决定目标。

## 显式保存事务

Platform取得逻辑预约，Service固定本次staged快照及generation，整个事务使用一个单调无符号毫秒期限；所有调用只给剩余预算，不每阶段重置。建议总预算2000ms（QSPI已READY，不包括首次Init），待Mock/实板审核。不足以满足BSP擦除500ms内部预留加预检/验读时，WREN之前拒绝。

1. 再扫两Slot，按上述规则确认当前有效副本与期望sequence相同；禁止仅用旧RAM缓存选择擦除对象。业务字段与当前有效payload相同则NO_CHANGE，无擦除、无sequence增加；首次EMPTY则不能NO_CHANGE而跳过持久化。
2. 目标为非active Slot；只有一份有效时不擦它；两份有效时擦较旧者。首次无有效副本的显式提交属于创建事务，掉电后可能仍只有默认，不声称存在旧持久配置。
3. 完整读取目标4096B作为EraseSector预检副本，并完成 [备份准入](Design.md) 的持久化安排。保存/核验成功后调用BSP EraseSector，逐字节FF证明由BSP完成。禁止擦完目标再去备份。
4. 新seq=current+1或首次0，写80B body，核对BSP结果及completed=80，再独立读回80B与预期、CRC及字段比较；Commit页仍须全FF。异常立即停，不写Commit。
5. 写16B Commit，核对BSP结果，再独立读取整Slot，跑与启动完全相同的验证器并比较原始预期配置；确认seq和payload一致。记录新persisted状态与 `pending_reboot=1`；active快照完全不变。
6. 不擦除/失效旧active，不把空闲时“顺手清理”带入本事务；下一次显式保存才可将它作为旧目标。释放逻辑预约。

每一步BSP异常保留首错与cleanup单独值、目标、sequence、写入完成未知、确认范围；不自动重试WREN/Program/Erase/Init/Recover。旧Slot在本次事务中不发送任何修改命令。未提交失败明确保留原配置；Commit命令已发出后的失败只能报 `OUTCOME_UNKNOWN`，不能断言“保存肯定未发生”。重启重新扫描可能选到旧或新完整记录；旧副本仍留存。保存成功但RAM报告前掉电同样可能启动新配置，这是提交线性化与响应丢失的正常边界。

Platform不得为消除UNKNOWN自行读状态并清除BSP FAULT，更不能用HAL Abort当Flash内部写停止证明。当前Recover拒绝有WREN/修改风险，必须尊重。MCU复位会丢RAM故障历史；磁盘格式能识别撕裂记录，并不解决Flash上电未稳定、MCU单独复位时Flash仍BUSY、WEL异常或任意物理损坏。

## 掉电安全论证及范围

预设：分区分配正确；每次只改目标扇区；合规供电/片选；非目标已提交Slot不受该次操作影响；CRC足够用于随机故障检测。结论是一次更新期间始终保留先前完整记录，候选记录仅在Commit、CRC和语义全部通过时可被选中。没有任何时刻同时擦两份，也没有启动自动修复。

擦除中断、body中断、commit中断分别可能留下非FF旧片段、半Header、半Commit；均按同一严格加载器判定。CRC32对某些损坏存在碰撞，Commit也不是原子安全元件；不承诺恶意篡改、任意欠压跨扇区损坏、两份同时坏或永久硬件失效。默认配置是降级运行策略，不是原用户配置恢复证明。

Winbond [Rev F 原厂手册](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf) §9.2/9.3要求合规供电和上/掉电片选跟随，写入前等待tPUW；其§8和§9的操作/时序不能被CRC替代。具体实装版本与供电监督仍需Q4核验。禁止用状态寄存器改写、Flash Reset或Suspend/Resume自动补救。
