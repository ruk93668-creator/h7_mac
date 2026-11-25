# M4 主线兼容性

使用 fetch 后经 GitHub 核对的 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。对生产候选 `b4044beec36476b1f327d961bca27d795ffeba90`，merge-base 就是 main，本次差异为 main 独有 0 / UI 独有 19 提交。只读 `git merge-tree --write-tree --name-only origin/main HEAD` 输出 `32d1d5db69a5d0bb43ec49f564ad4b5b01ba6a18`，等于候选树，退出 0。未建立 merge commit 或移动引用。此结论只适用于核对时的 main。

- **工程 XML**：只新增 7 个 CM4 Application 源文件登记。CM7 Target 扫描及字节切片与 main 相同。相对 main 未改变 `uvoptx`。
- **公共接口和启动**：Common、Board、CM7、CM4 Device/BSP/Framework/Service/Core、CM4 启动文件、Scatter 与 IOC 同 main。V3 ABI、SRAM4 共享窗口、CM4 Flash 起址以及原保留栈不变。
- **I²C2 和引脚**：未发现本基线其他已启用模块占用 PI6、PC15、PI9。U7 的 PC15 与 LSE 互斥，软件检测 LSEON 后跳过输入初始化。仍须核对实际时钟、板级上拉及编码器方向。合并未来的 RTC/低功耗类改动时需再次审查，禁止为输入关闭 LSE。
- **其他支线的文本重叠**：已核对 application-v3-media-usb、BLE、Ethernet（两支）、QSPI 和 USB MSC。这些候选与 UI 的共同改动文件为 `h7_mac.uvprojx`，未见已提交生产 C 文件重叠。这是潜在重叠清单，不是对这些支线执行过合并，也不保证其外设和时钟兼容。
- **许可**：Apache-2.0 许可全文、固定上游 SHA、素材变换与来源均已记录。未引入外部阻塞驱动或硬件示例代码。
- `git -c core.whitespace=cr-at-eol diff --check origin/main...HEAD` 通过。实际合并前按届时远端 main 重做只读检查与最终构建。
