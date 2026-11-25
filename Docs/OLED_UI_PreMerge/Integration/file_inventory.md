# 文件清单

最终 UI 目录 `CM4/Application/UI/`

```text
Assets/oled_ui_assets.inc
Assets/oled_ui_bitmaps.inc
oled_ui.c
oled_ui.h
oled_ui_anim.c
oled_ui_anim.h
oled_ui_draw.c
oled_ui_draw.h
oled_ui_font.c
oled_ui_font.h
oled_ui_input.c
oled_ui_input.h
oled_ui_menu.c
oled_ui_menu.h
oled_ui_page.c
oled_ui_page.h
oled_ui_status.c
oled_ui_status.h
oled_ui_tools.c
oled_ui_tools.h
```

| 原路径 | 新路径 |
| --- | --- |
| `CM4/Application/oled_ui_assets.inc` | `CM4/Application/UI/Assets/oled_ui_assets.inc` |
| `CM4/Application/oled_ui_assets.inc` | `CM4/Application/UI/Assets/oled_ui_bitmaps.inc` |
| `CM4/Application/oled_ui.c` | `CM4/Application/UI/oled_ui.c` |
| `CM4/Application/oled_ui.h` | `CM4/Application/UI/oled_ui.h` |
| `CM4/Application/oled_ui_anim.c` | `CM4/Application/UI/oled_ui_anim.c` |
| `CM4/Application/oled_ui_anim.h` | `CM4/Application/UI/oled_ui_anim.h` |
| `CM4/Application/oled_ui_draw.c` | `CM4/Application/UI/oled_ui_draw.c` |
| `CM4/Application/oled_ui_draw.h` | `CM4/Application/UI/oled_ui_draw.h` |
| `CM4/Application/oled_ui_compat.c/.h` | `CM4/Application/UI/oled_ui_font.c` |
| `CM4/Application/oled_ui_compat.c/.h` | `CM4/Application/UI/oled_ui_font.h` |
| `CM4/Application/oled_ui_input.c` | `CM4/Application/UI/oled_ui_input.c` |
| `CM4/Application/oled_ui_input.h` | `CM4/Application/UI/oled_ui_input.h` |
| `CM4/Application/oled_ui_menu.c` | `CM4/Application/UI/oled_ui_menu.c` |
| `CM4/Application/oled_ui_menu.h` | `CM4/Application/UI/oled_ui_menu.h` |
| `CM4/Application/oled_ui_page.c` | `CM4/Application/UI/oled_ui_page.c` |
| `CM4/Application/oled_ui_page.h` | `CM4/Application/UI/oled_ui_page.h` |
| `CM4/Application/oled_ui_status.c` | `CM4/Application/UI/oled_ui_status.c` |
| `CM4/Application/oled_ui_status.h` | `CM4/Application/UI/oled_ui_status.h` |
| `CM4/Application/oled_ui_compat.c/.h` | `CM4/Application/UI/oled_ui_tools.c` |
| `CM4/Application/oled_ui_compat.c/.h` | `CM4/Application/UI/oled_ui_tools.h` |

以下是主线与构建候选的完整逐文件差异清单。文档交付提交另附本目录。

```text
A	CM4/Application/UI/Assets/oled_ui_assets.inc
A	CM4/Application/UI/Assets/oled_ui_bitmaps.inc
R083	CM4/Application/oled_ui.c	CM4/Application/UI/oled_ui.c
R088	CM4/Application/oled_ui.h	CM4/Application/UI/oled_ui.h
A	CM4/Application/UI/oled_ui_anim.c
A	CM4/Application/UI/oled_ui_anim.h
A	CM4/Application/UI/oled_ui_draw.c
A	CM4/Application/UI/oled_ui_draw.h
A	CM4/Application/UI/oled_ui_font.c
A	CM4/Application/UI/oled_ui_font.h
A	CM4/Application/UI/oled_ui_input.c
A	CM4/Application/UI/oled_ui_input.h
A	CM4/Application/UI/oled_ui_menu.c
A	CM4/Application/UI/oled_ui_menu.h
A	CM4/Application/UI/oled_ui_page.c
A	CM4/Application/UI/oled_ui_page.h
A	CM4/Application/UI/oled_ui_status.c
A	CM4/Application/UI/oled_ui_status.h
A	CM4/Application/UI/oled_ui_tools.c
A	CM4/Application/UI/oled_ui_tools.h
M	CM4/Application/app_oled.c
M	CM4/Application/app_oled.h
A	CM4/BSP/bsp_u7.c
A	CM4/BSP/bsp_u7.h
A	Docs/OLED_UI_PreMerge/after-fix.log.txt
A	Docs/OLED_UI_PreMerge/architecture_audit.md
A	Docs/OLED_UI_PreMerge/before-fix.log.txt
A	Docs/OLED_UI_PreMerge/build_verification.md
A	Docs/OLED_UI_PreMerge/cm4-build.log.txt
A	Docs/OLED_UI_PreMerge/cm4-debug-build.log.txt
A	Docs/OLED_UI_PreMerge/cm7-build.log.txt
A	Docs/OLED_UI_PreMerge/code_review.md
A	Docs/OLED_UI_PreMerge/default-regression.log.txt
A	Docs/OLED_UI_PreMerge/default-results.json
A	Docs/OLED_UI_PreMerge/feature_matrix.md
A	Docs/OLED_UI_PreMerge/full-regression.log.txt
A	Docs/OLED_UI_PreMerge/full-results.json
A	Docs/OLED_UI_PreMerge/known_issues.md
A	Docs/OLED_UI_PreMerge/main_compatibility.md
A	Docs/OLED_UI_PreMerge/merge_readiness.md
A	Docs/OLED_UI_PreMerge/regression_results.md
A	Docs/OLED_UI_PreMerge/scope.md
A	Docs/UI1/baseline-keil.log.txt
A	Docs/UI1/candidate-keil.log.txt
A	Docs/UI1/hardware-supplement.md
A	Docs/UI1/results.json
A	Docs/UI1/ui1-regression.log.txt
A	Docs/UI1/verification.md
A	Docs/UI2/acceptance.md
A	Docs/UI2/default-results.json
A	Docs/UI2/menu-results.json
A	Docs/UI2/page-design.md
A	Docs/UI2/u7-results.json
A	Docs/UI2/u7.md
A	Docs/UI2/verification.md
A	Docs/UI36/20fps.md
A	Docs/UI36/OLED_UI.Apache-2.0.txt
A	Docs/UI36/acceptance.md
A	Docs/UI36/compat-keil-cm4.log.txt
A	Docs/UI36/compat-keil-cm7.log.txt
A	Docs/UI36/compat-keil-debug.log.txt
A	Docs/UI36/compat-results.json
A	Docs/UI36/keil-debug.log.txt
A	Docs/UI36/keil-release.log.txt
A	Docs/UI36/oled-ui-compat-verification.md
A	Docs/UI36/oled-ui-upstream.md
A	Docs/UI36/previews/cursor.gif
A	Docs/UI36/previews/editor.gif
A	Docs/UI36/previews/frame-sheet.png
A	Docs/UI36/previews/gallery.gif
A	Docs/UI36/previews/integer.gif
A	Docs/UI36/previews/marquee.gif
A	Docs/UI36/previews/tiles.gif
A	Docs/UI36/previews/toggle.gif
A	Docs/UI36/previews/viewport.gif
A	Docs/UI36/results.json
A	Docs/UI36/system-monitor-design.md
A	Docs/UI36/ui3.md
A	Docs/UI36/ui4.md
A	Docs/UI36/ui5.md
M	MDK-ARM/h7_mac.uvprojx
A	Tests/OledMonitor/HalStub/stm32h7xx_hal.h
A	Tests/OledMonitor/Ui20FpsCase.inc
A	Tests/OledMonitor/Ui4Case.inc
A	Tests/OledMonitor/Ui5Case.inc
A	Tests/OledMonitor/Ui6Case.inc
A	Tests/OledMonitor/UiCompatCase.inc
A	Tests/OledMonitor/UiPreMergeCase.inc
A	Tests/OledMonitor/UiSysCase.inc
M	Tests/OledMonitor/monitor_test.c
A	Tests/OledMonitor/render_compat.py
M	Tests/OledMonitor/run_tests.py
A	Tests/OledMonitor/ui1_cases.inc
A	Tests/OledMonitor/ui2_cases.inc
A	Tests/OledMonitor/ui2_input_cases.inc
```

可用 `git diff --binary 62b42d7c0b7df9eadafa00953ee8ce1229da9826 72140cd55ebc82a8ce4d6e5d5c8a6c5f9a143c6a` 导出完整补丁。
