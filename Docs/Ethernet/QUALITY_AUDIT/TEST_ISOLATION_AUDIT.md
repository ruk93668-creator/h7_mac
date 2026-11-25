# Q6 测试隔离

结论 **PASS**（构建边界），ETH-Q09 保留。正式 CM7 XML 及 MAP 检查 Tests、mock、fixture 与独立 main。E4/E4.2 真 lwIP 与模拟 MAC，E5 真 lwIP 双 TCP 加 UDP，E6 样片由独立工程注入，E7 使用独立 Camera Provider 和 V1 BSP。正式 CM7 只链接公共 Service 与 Platform，样片数据不在正式镜像。共享 sys_now 已抽取，Host 注入受控时间；V1 测试 BSP 与正式 BSP 分别保留，未进入正式编译。测试入口编译和回归结果见 BUILD_REGRESSION.md。
