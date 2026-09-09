# PlanePet 3.0.10 关于页字号调整

范围仅限关于、隐私说明、第三方许可三个页面：正文和链接逻辑字号 18 → 16，标题 29 → 25。保留 DPI 原生文字渲染、窗口大小、图标、链接位置、Ding 署名及 planepet_public@163.com 邮箱。桌宠、游戏、帮助和历史战绩字号不变。

完整编译成功；实际桌面窗口回归 767 PASS / 0 FAIL，包含 96/120/144/192 DPI 下三类字体大小、邮箱不截断、阅读滚动和返回检查。实际主页及隐私页截图已经读图检查。

回归报告：`dist/release-regression-5d4292a2e3d64e62bcd2842dd2f4c9ee/中文 用户路径/检查结果.txt`。

单端与双端发行包静态检查、内嵌文档和组件哈希比对、帮助文案一致性及 Git diff 空白检查通过。未修改服务器或用户数据，未上传 OSS/GitHub，未终止用户正在运行的 3.0.9。

单端 EXE：`dist/PlanePet-Public-Single-3.0.10/PlanePet.exe`

SHA-256：`4a8624d2eb59e611f37c5c47bbd4d6fc49109284d68c9e79dcf83742b3aa063c`

完整包：`dist/PlanePet-Public-Single-3.0.10.zip`

仍是未签名候选版本，本轮不是公网双机或其他发布待办的验收。
