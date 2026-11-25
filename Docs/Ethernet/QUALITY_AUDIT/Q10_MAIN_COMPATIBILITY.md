# Q10.2 Main 兼容性

以 GitHub 读取的 `main@851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71` 压缩归档为起点。以本地 Git 的共同基线 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb` 比较双方，只叠加 Ethernet 专属差异。操作在临时文件树中进行，未产生 Git 合并提交。

## Keil 工程处理

- 采用 XML 字段级整合。CM4 目标与 main 的规范化 XML 完全相同。CM7 已有分组与设置来自 main，既有 Application V3、USB、Storage 和 Camera 相关源文件清单保留。只追加四个 Ethernet 组、对应头文件路径、两项协议宏和 Ethernet 链接保留项。
- 保留 main 的编译选项、现有链接保留项和 Scatter 路径。CM7 Scatter 内容叠加 Ethernet CPU 窗口，正式启动沿用 main 的其余配置并包含 Ethernet Region 4 设置。XML 检查两个目标的编译源文件无重复或缺失。
- `.gitignore` 两边均追加输出目录。手动保留 main 的 Application V3 和分支的 Ethernet 两条规则。
- SHA-256 核对 CM7 BSP、Media、Application V3、CM4 和 Keil RTE 文件与 main 归档相同。CM4 OLED 所用的 UI 资源由 main 保留。候选未把 E7 测试板 BSP 或样片引入正式目标。
- 两套正式 CM7 MAP 都检查 Ethernet DMA 在 0x24078000，占 12544 B。Ethernet CPU 区在 0x24010000，占 43232 B，余 22304 B。候选及整合版的 SDMMC 仍用 0x24000000（32 KiB），共享 IPC 仍用 0x38000000。正式镜像的 ETH IRQ、HAL Init 和 Device Registry 入口均只有一个最终符号。

[共同基线 Keil](Q10_Evidence/base-keil.xml) · [Main Keil](Q10_Evidence/main-keil.xml) · [整合 Keil](Q10_Evidence/integration-keil.xml) · [字段对照](Q10_Evidence/keil-mapping.json)

[完整整合 Diff](Q10_Evidence/integration-vs-main.diff) · [Diff 统计](Q10_Evidence/integration-vs-main.stat.txt) · [整合文件 SHA-256](Q10_Evidence/integration-sha256.json) · [整合覆盖清单](Q10_Evidence/integration-overlay.json) · [整合覆盖包](Q10_Evidence/integration-overlay.zip)

兼容结论覆盖指定 main SHA 的编译、链接和主机回归。正式板上 USB、Storage、Camera、OLED 的运行行为仍需实板证据。构建与测试明细见 [Q10 最终报告](Q10_FINAL_REPORT.md)。

[Main 相对共同基线的完整 Keil 差异](Q10_Evidence/base-to-main-keil.diff) · [Ethernet 候选相对共同基线的完整 Keil 差异](Q10_Evidence/base-to-candidate-keil.diff) · [临时整合的 Keil 差异](Q10_Evidence/main-to-integration-keil.diff)
