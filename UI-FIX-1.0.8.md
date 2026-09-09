# 1.0.8：历史战绩使用 A 组像素爱心

2026-09-06。本次仅替换历史战绩的血量素材及本地编译打包，未改战斗 HUD、联网协议、统计或真实用户存档。未部署 ECS / OSS、未上传 GitHub，未操作 FingerKnight。

## 实现

- 将用户选定的第一组 A 原始 PNG 原样复制到 `assets/hearts/history-classic-a.png`，RCDATA 161 内嵌到客户端，再由公网单端 / 双端启动器内嵌客户端。无需额外图片文件，不依赖启动目录。
- 蓝方剩余 HP 用蓝心、红方用红心，已扣除 HP 用同组灰色心形槽；每方固定三个位置。
- 按同尺寸源帧采样，排除概念图的外围留白；20 × 18 逻辑像素、21 像素中心距，最近邻绘制并保留 alpha。
- 使用既有 `ImageCanvas` 处理原生 GDI+ 与窗口 DPI 映射，缓存图片和内存流；素材加载失败则回退原心形，不让血量消失。
- 保留历史窗口大小、红蓝 D2 飞机的中心逆时针 45° 旋转、日期冒号间距、最近十条规则与存档格式。保留 1.0.7 包。

源图、采样区和原提示词索引见 `assets/hearts/README.md`。此次没有重新生成或修改选中的图片。

## 验证

- `build.ps1 -BuildPublic` 通过（`-O2 -Wall -Wextra`，无编译警告）。
- `verify_release_hardening.ps1` 最终 **457/457 通过**，涵盖 96 / 120 / 144 / 192 DPI（100% / 125% / 150% / 200%）、0–3 HP、灰槽、透明凹口、模型方向、时间宽度、空列表、真实窗口打开 / 关闭、独立测试战绩保存 / 加载与 GDI 句柄稳定性。
- 按原始大小读图检查四种 DPI 的完整十行与空历史。最初 125% 的凹口检测取整落在相邻边框像素：逻辑中心 187.5 被 GDI 取为 188，而 GDI+ 的像素中心是 187.5，即物理像素 187。改为在相邻物理像素检查透明凹口后通过，保留了心形与角落检查。
- `verify_fix_regressions.ps1` 通过：客户端 / 服务端边界、29 项网关 / 匿名统计测试、隔离本地完整对战、匹配 / 取消 / 恢复 / 解绑 / 版本兼容、勿扰 / 表情、升级安装 / 回滚 / 错误哈希 / 中断恢复、公网单双端 ZIP 静态检查。
- `verify_game_render.ps1` 通过，地图 / HUD 边界与桌宠追击运动不变；最终四种 DPI 的游戏截图与 1.0.7 SHA256 全部一致。
- `tests/verify_embedded_components.ps1 -Version 1.0.8` 通过：单端 / 双端内嵌程序为本次产物，客户端资源 161 与用户选中的原图 SHA256 完全一致。
- `git diff --check` 通过。旧 1.0.7 单端 / 双端 ZIP 哈希未变化。

最终原生报告 / 图像目录：

`dist/release-regression-d97aa660bc564a439010214b0fdce3f4/中文 用户路径/`

其中 `history-120.png` 是 125% 下的真实绘制函数输出，使用合成战绩，不是用户数据。

## 交付文件与 SHA256

- 单端 EXE：`dist/PlanePet-Public-Single-1.0.8/PlanePet.exe`
  - `a62c717cb19b7fe77324a93a00f651900847bb1e057f105462b26252e44eb5cc`
- 单端 ZIP：`dist/PlanePet-Public-Single-1.0.8.zip`
  - `a14956d11d53ea59fb6f0e48e556ad54d7f21f8a088c6dba2ba805ff0bc461b5`
- 单机双端公网 ZIP：`dist/PlanePet-Public-DualLocal-1.0.8.zip`
  - `b878887f048b0e8b084f53b8fd007d361ff1e1b40d561c74a6a35a759e35ea60`
- 内嵌客户端：`dist/PlanePetClient.exe`
  - `df62324be7ccffdb9e0321880a374e5796fcff2ff482f516492bfd72e25231ce`

退出旧程序再运行新 EXE。联网地址沿用既有公网配置；未重复人工跨公网双人实战，也未在线发布此版升级清单。此 EXE 与旧测试版一样没有 Authenticode 签名。
