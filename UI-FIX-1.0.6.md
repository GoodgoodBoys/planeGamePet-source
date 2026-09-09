# 1.0.6 悬浮窗文字渲染修复

2026-09-06。仅在本地生成新版本；未部署 ECS、发布 OSS/GitHub，也未更改用户存档、统计数据或 FingerKnight。

## 修改范围

- 新增 `desktop/pet_text.h`，仅用于悬浮窗中有不透明底色的消息卡、输入区和按钮。使用 Microsoft YaHei UI、ClearType，以及负的 `lfHeight` 明确指定字形 em 像素高度。
- 普通邀请/按钮使用 14px em，自身勿扰状态使用 16px em。绑定页按各区域单独设置，避免机械地将旧的正数 cell 高度全部改为同值负数，导致字体膨胀与裁切。
- 绑定页长错误提示使用 12px em；文字矩形从 y=34..64 调整为 y=30..66，使两行完整容纳。输入框、点击区域和卡片本身未移动。
- 绘制后恢复字体、背景模式和文字颜色，释放 GDI 字体对象。
- 桌宠仍为 280×150 物理像素，游戏仍为 240×372；没有修改飞机/云/表情、游戏字体、帮助/历史字体、联网协议、统计或升级行为。
- 老的 `TextWithFace` 保留给游戏、帮助/历史以及 Emoji 字体回退；没有全局修改 Windows 字体设置。

ClearType 只在既有不透明卡片上原尺寸绘制，然后随帧 1:1 BitBlt。没有将文字先转成低分辨率图片再放大，也没有在透明 sprite 边缘使用子像素文本。

## 检查结果

- `build.ps1 -BuildPublic`：通过，使用 `-O2 -Wall -Wextra` 编译。
- `verify_release_hardening.ps1`：真实 Windows 桌面会话 414/414 项通过。覆盖 96/120/144/192 DPI、邀请/等待/勿扰、绑定输入/搜索、长错误、断线提示、菜单、托盘和窗口大小位置稳定。
- 新增字体参数/实际 em 高度/字体可用性校验、真实 GDI 文本尺寸校验、1000 次文字绘制无 GDI 句柄泄漏及 DC 状态恢复检查。
- `verify_fix_regressions.ps1`：通过。包括客户端和服务端边界、29 项网关/统计测试、本地完整对战、匹配取消/过期/恢复/解绑/版本兼容、勿扰/表情、升级安装/回滚/坏哈希/中断恢复，以及单端/双端 ZIP 静态检查。
- `verify_update_windows.ps1`：状态和原生窗口检查通过，关闭按钮、Esc、隐藏、邀请/游戏打断均通过。
- `verify_game_render.ps1`：游戏 HUD/地图边界、桌宠模型和追击运动检查通过。
- `tests/verify_embedded_components.ps1 -Version 1.0.6`：单端/双端内嵌客户端、隧道和升级器均与本次编译产物一致。
- 已按原始像素尺寸查看旧/新文字对比、绑定输入/搜索、两行错误、断线、邀请、勿扰及最长状态文字图像。
- 初次受限环境的托盘两项未通过；在真实桌面会话复测全部通过。新增的长错误提示测量最初发现高度不足，修正文字区域后通过，未忽略失败项。
- 没有重复进行跨公网人工双人实战或线上自动升级发布测试；对应逻辑本次未修改。

窗口报告与渲染图：

`dist/release-regression-8e060dbb74a04a5b90b850eae59ec163/中文 用户路径/`

其中 `pet-text-before-after.png` 左列为 1.0.5 旧绘制参数，右列为 1.0.6 新参数。子像素文字在屏幕上原尺寸显示时评估，不以图片缩放或压缩后的效果判断。

更新窗口报告：

`C:/Users/81228/AppData/Local/Temp/plane-pet-update-window-dd740afed4d542828a005824fc5982af/window-test.txt`

## 本地交付

- 单端公网：`dist/PlanePet-Public-Single-1.0.6/PlanePet.exe`
- 单端 ZIP：`dist/PlanePet-Public-Single-1.0.6.zip`
- 单机双端公网 ZIP：`dist/PlanePet-Public-DualLocal-1.0.6.zip`
- 保留原 1.0.5 两个 ZIP。退出旧程序后运行新 EXE，沿用原存档，无需重新绑定。新版本仍未附 Windows Authenticode 签名。

SHA256：

- 单端 EXE：`5af3154023b7a588c52e421dd66d179a05c2f580c7fafa029a6de37d08be3baf`
- 单端 ZIP：`b9c7928ae29bf8a97f29d35efc6b0f387d56579d9bce41c57518b2a253d8e1ea`
- 双端 ZIP：`9ab0dcc245458dedb59d6307eefd216b0d1e060d25be8d8cb290b0bec586b9f9`
- 内嵌客户端：`c31aa50eb2ab5c56efbec4098f893445f5131b08f330feecd24be3b31f9d3c64`
