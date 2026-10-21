# SDRAM诊断已从正式固件移出

用户反馈110MHz实板测试通过后，已清理正式固件诊断。历史验收报告及clock_fix保留。

sdram_check.c/.h现在位于diagnostics/，仅供独立测试；g_sdram_*观察变量及SDRAM_CheckRun自动Initcall已不在正式Target中。正式BSP仍在BUS阶段初始化SDRAM，错误由SysResult_t和Framework返回main的init_error。

本目录run_tests.py仍可执行独立1KB诊断；本轮完整结果和新增“正式初始化零数据区访问”验收见../SdramCleanup/验收报告.md。
