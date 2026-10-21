# CM4 OLED异步刷新模拟验收

真实源码 + Mock BSP；ARMCC5编译ARM可执行程序，Unicorn实际运行。

| 编号 | 用例 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|---|
| 1 | 正常整帧 | 调用Refresh，依次完成8笔传输，每笔后调用Process | CMD0→DATA0→CMD1→DATA1→CMD2→DATA2→CMD3→DATA3；命令和512字节数据全部匹配，回IDLE | 最终记录返回0；错误返回[]；状态IDLE；页3；事务8笔；断言605；越界监控0次 | PASS |
| 2 | 命令未完成 | Refresh成功后连续Process 12次，不交付结果 | 每次SYS_OK，WAIT_CMD、页号、事务数量不变 | 最终记录返回0；错误返回[]；状态WAIT_CMD；页0；事务1笔；断言54；越界监控0次 | PASS |
| 3 | 数据未完成 | CMD0完成并启动DATA0，连续Process 12次，不交付结果 | 每次SYS_OK，WAIT_DATA、页号、事务数量不变 | 最终记录返回0；错误返回[]；状态WAIT_DATA；页0；事务2笔；断言63；越界监控0次 | PASS |
| 4 | 命令期间重复Refresh | WAIT_CMD时再次Refresh | BUSY=-5；状态、页号不变，不调用BSP提交 | 最终记录返回-5；错误返回[-5]；状态WAIT_CMD；页0；事务1笔；断言9；越界监控0次 | PASS |
| 5 | 数据期间重复Refresh | WAIT_DATA时再次Refresh | BUSY=-5；状态、页号不变，不调用BSP提交 | 最终记录返回-5；错误返回[-5]；状态WAIT_DATA；页0；事务2笔；断言18；越界监控0次 | PASS |
| 6 | 最后一页边界 | 完成DATA3，然后连续Process 10次 | 回IDLE，页号保持3；只有8笔事务，不产生CMD4，不再取BSP结果 | 最终记录返回0；错误返回[]；状态IDLE；页3；事务8笔；断言592；越界监控0次 | PASS |
| 7 | 连续两帧 | 完成第一帧后更换全部帧数据，重新Refresh并完成第二帧 | 第二帧从CMD0启动，共16笔事务，第二帧使用新数据，两帧分别回IDLE | 最终记录返回0；错误返回[]；状态IDLE；页3；事务16笔；断言1210；越界监控0次 | PASS |
| 8 | CMD0完成返回IO | 正常推进到CMD0，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页0；事务1笔；断言10；越界监控0次 | PASS |
| 9 | DATA0完成返回IO | 正常推进到DATA0，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页0；事务2笔；断言19；越界监控0次 | PASS |
| 10 | CMD1完成返回IO | 正常推进到CMD1，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页1；事务3笔；断言153；越界监控0次 | PASS |
| 11 | DATA1完成返回IO | 正常推进到DATA1，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页1；事务4笔；断言162；越界监控0次 | PASS |
| 12 | CMD2完成返回IO | 正常推进到CMD2，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页2；事务5笔；断言296；越界监控0次 | PASS |
| 13 | DATA2完成返回IO | 正常推进到DATA2，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页2；事务6笔；断言305；越界监控0次 | PASS |
| 14 | CMD3完成返回IO | 正常推进到CMD3，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页3；事务7笔；断言439；越界监控0次 | PASS |
| 15 | DATA3完成返回IO | 正常推进到DATA3，完成结果为-9 | Process原样返回-9，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-9]；状态IDLE；页3；事务8笔；断言448；越界监控0次 | PASS |
| 16 | CMD0完成返回TIMEOUT | 正常推进到CMD0，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页0；事务1笔；断言10；越界监控0次 | PASS |
| 17 | DATA0完成返回TIMEOUT | 正常推进到DATA0，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页0；事务2笔；断言19；越界监控0次 | PASS |
| 18 | CMD1完成返回TIMEOUT | 正常推进到CMD1，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页1；事务3笔；断言153；越界监控0次 | PASS |
| 19 | DATA1完成返回TIMEOUT | 正常推进到DATA1，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页1；事务4笔；断言162；越界监控0次 | PASS |
| 20 | CMD2完成返回TIMEOUT | 正常推进到CMD2，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页2；事务5笔；断言296；越界监控0次 | PASS |
| 21 | DATA2完成返回TIMEOUT | 正常推进到DATA2，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页2；事务6笔；断言305；越界监控0次 | PASS |
| 22 | CMD3完成返回TIMEOUT | 正常推进到CMD3，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页3；事务7笔；断言439；越界监控0次 | PASS |
| 23 | DATA3完成返回TIMEOUT | 正常推进到DATA3，完成结果为-8 | Process原样返回-8，状态回IDLE，不发送下一笔 | 最终记录返回0；错误返回[-8]；状态IDLE；页3；事务8笔；断言448；越界监控0次 | PASS |
| 24 | CMD0提交返回BUSY | 第1笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页0；事务0笔；断言6；越界监控0次 | PASS |
| 25 | DATA0提交返回BUSY | 第2笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页0；事务1笔；断言10；越界监控0次 | PASS |
| 26 | CMD1提交返回BUSY | 第3笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页1；事务2笔；断言19；越界监控0次 | PASS |
| 27 | DATA1提交返回BUSY | 第4笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页1；事务3笔；断言153；越界监控0次 | PASS |
| 28 | CMD2提交返回BUSY | 第5笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页2；事务4笔；断言162；越界监控0次 | PASS |
| 29 | DATA2提交返回BUSY | 第6笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页2；事务5笔；断言296；越界监控0次 | PASS |
| 30 | CMD3提交返回BUSY | 第7笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页3；事务6笔；断言305；越界监控0次 | PASS |
| 31 | DATA3提交返回BUSY | 第8笔提交返回-5 | Refresh或Process原样返回-5，回IDLE，无额外传输 | 最终记录返回0；错误返回[-5]；状态IDLE；页3；事务7笔；断言439；越界监控0次 | PASS |
| 32 | CMD0提交返回IO | 第1笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页0；事务0笔；断言6；越界监控0次 | PASS |
| 33 | DATA0提交返回IO | 第2笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页0；事务1笔；断言10；越界监控0次 | PASS |
| 34 | CMD1提交返回IO | 第3笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页1；事务2笔；断言19；越界监控0次 | PASS |
| 35 | DATA1提交返回IO | 第4笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页1；事务3笔；断言153；越界监控0次 | PASS |
| 36 | CMD2提交返回IO | 第5笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页2；事务4笔；断言162；越界监控0次 | PASS |
| 37 | DATA2提交返回IO | 第6笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页2；事务5笔；断言296；越界监控0次 | PASS |
| 38 | CMD3提交返回IO | 第7笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页3；事务6笔；断言305；越界监控0次 | PASS |
| 39 | DATA3提交返回IO | 第8笔提交返回-9 | Refresh或Process原样返回-9，回IDLE，无额外传输 | 最终记录返回0；错误返回[-9]；状态IDLE；页3；事务7笔；断言439；越界监控0次 | PASS |
| 40 | CMD0提交返回TIMEOUT | 第1笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页0；事务0笔；断言6；越界监控0次 | PASS |
| 41 | DATA0提交返回TIMEOUT | 第2笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页0；事务1笔；断言10；越界监控0次 | PASS |
| 42 | CMD1提交返回TIMEOUT | 第3笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页1；事务2笔；断言19；越界监控0次 | PASS |
| 43 | DATA1提交返回TIMEOUT | 第4笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页1；事务3笔；断言153；越界监控0次 | PASS |
| 44 | CMD2提交返回TIMEOUT | 第5笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页2；事务4笔；断言162；越界监控0次 | PASS |
| 45 | DATA2提交返回TIMEOUT | 第6笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页2；事务5笔；断言296；越界监控0次 | PASS |
| 46 | CMD3提交返回TIMEOUT | 第7笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页3；事务6笔；断言305；越界监控0次 | PASS |
| 47 | DATA3提交返回TIMEOUT | 第8笔提交返回-8 | Refresh或Process原样返回-8，回IDLE，无额外传输 | 最终记录返回0；错误返回[-8]；状态IDLE；页3；事务7笔；断言439；越界监控0次 | PASS |
| 48 | 空指针 | Refresh(NULL)、Process(NULL) | 均返回INVALID_ARG=-1，无BSP调用 | Refresh(NULL)、Process(NULL)均断言返回-1；最终记录返回0；错误返回[]；状态IDLE；页0；事务0笔；断言5；越界监控0次 | PASS |
| 49 | 无效状态 | refresh_state=99，Process | 返回INVALID_STATE=-4，无BSP调用 | 最终记录返回-4；错误返回[-4]；状态99；页0；事务0笔；断言4；越界监控0次 | PASS |
| 50 | 通信错误后重新刷新 | DATA1完成返回IO，然后发起完整新帧 | 错误原样上报；随后从CMD0重启并完成整帧 | 最终记录返回0；错误返回[-9]；状态IDLE；页3；事务12笔；断言762；越界监控0次 | PASS |

合计 50，PASS 50，FAIL 0，未执行0；断言11156项。

正常第一帧实际事务：CMD0 → DATA0 → CMD1 → DATA1 → CMD2 → DATA2 → CMD3 → DATA3

完整命令和像素字节、逐步状态记录见results.json。测试不模拟SSD1306电气行为；Mock按当前BSP契约在WriteAsync返回前复制数据，并保持单笔事务，结果消费后才可再次提交。
对象前后16字节监控读写并检查哨兵；全部正常页数据逐字节比较。未向非法page值强行注入内存损坏，本报告的越界结论针对有效刷新状态链。

## 实际编译命令

```text
D:\32\ARM\ARMCC\Bin\armcc.exe --vsn
D:\32\ARM\ARMCC\Bin\armcc.exe --cpu Cortex-M4 --c99 -O0 -I D:\32\STM32H7\h7_mac\CM4\Device\Inc -I D:\32\STM32H7\h7_mac\CM4\BSP -I D:\32\STM32H7\h7_mac\Common\Inc -I D:\32\STM32H7\h7_mac\Board\Inc -c D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\oled_async_test.c -o D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\oled_async_test.o
D:\32\ARM\ARMCC\Bin\armlink.exe D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\oled_async_test.o --entry Oled_TestRun --scatter D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\test.sct --keep=*(.device_export) --no_scanlib -o D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\oled_async_test.axf
```
