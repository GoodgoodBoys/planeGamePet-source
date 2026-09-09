# 1.0.9：对战窗口同步 A 组像素爱心

2026-09-06。仅客户端视觉接入、验证和本地打包，未修改联网 / 统计 / 血量计算 / 存档协议，未操作 ECS、OSS、GitHub 或 FingerKnight。

## 修改

- 真实游戏 `DrawGameHud` 使用用户选中的 A 组蓝心 / 红心 / 灰色心槽，和历史战绩共用资源 161、同一内存缓存及最近邻采样函数。
- 每方保留三个心位。蓝方剩余 HP 从左排列，红方沿用原来的镜像顺序从右排列；非满血位置显示灰槽，而不留空。0 HP、双方视角、倒计时和结算均适用。
- HUD 爱心 13 × 12 像素，中心距 14；状态栏仍为 240 × 52，完整游戏客户区仍为 240 × 372，地图仍为 240 × 320。飞机、倒计时、延迟位置不变。
- 将公用采样区移到 `desktop/heart_assets.h`，历史列表保留 20 × 18 的原有显示尺寸。资源读取失败时仍有原心形回退，灰槽也保留。
- 更新 README 与素材使用说明。原始 PNG 与旧 1.0.8 包未改动。

## 验证

- `build.ps1 -BuildPublic` 通过，`-O2 -Wall -Wextra` 编译无警告。
- `verify_release_hardening.ps1`：**464/464 通过**。
  - 新增两个视角各 16 种双方 HP 组合（0–3）；验证蓝 / 红 / 灰的实际像素、镜像血量顺序、心形凹口、透明角落和间距。
  - 满血覆盖重绘到残血 / 空血，检查无旧血量残影；倒计时 / Playing / Finished、异常 HP 限幅、未收到快照的满血预览均通过。
  - HUD 重复绘制 120 次无 GDI 句柄增长；历史战绩、邀请、勿扰、菜单、窗口、存档与四种 DPI 原回归全部通过。
- `verify_fix_regressions.ps1` 通过：客户端 / 服务端边界、29 项网关 / 统计、隔离本地完整对战、匹配生命周期、版本兼容、勿扰 / 表情、升级安装 / 回滚 / 恢复及单双端 ZIP 静态检查。
- `verify_game_render.ps1` 通过，地图边界 / 两端移动对称及桌宠追击运动不变。
- 比较最终 96 / 120 / 144 / 192 DPI 原生游戏图与 1.0.8：每幅仅有 585 像素变化，全部在两侧爱心区域内；地图、状态栏其余内容及窗口尺寸逐像素未变。四种 DPI 的历史战绩图 SHA256 均完全相同。
- 按原始尺寸读图检查完整游戏界面、两端全部 HP 组合的 HUD 图集。
- `tests/verify_embedded_components.ps1 -Version 1.0.9` 通过：单双端启动器内嵌本次客户端 / 隧道 / 升级器，心形 PNG 的 SHA256 仍与选定 A 原图一致。
- `git diff --check` 通过。

原生测试报告及图像：

`dist/release-regression-d6de80ba983c43939a9b341aff97f816/中文 用户路径/`

`game-120.png` 为真实对战绘制函数的隔离测试预览；`combat-hp-view-1.png`、`combat-hp-view-2.png` 为双方视角 0–3 HP 图集。不使用真实用户存档；本次未重复人工跨公网双人实战或发布在线升级清单。

## 交付

- 单端公网 EXE：`dist/PlanePet-Public-Single-1.0.9/PlanePet.exe`
  - SHA256 `898eebdc59700746b246801c9118026e53aa0788ce7d323711a9d00a4cf99210`
- 单端 ZIP：`dist/PlanePet-Public-Single-1.0.9.zip`
  - SHA256 `db1e9055537bfd25531fa1828d01715ffcef97a4555b5ff3ee7d33fd7b6950c6`
- 单机双端公网 ZIP：`dist/PlanePet-Public-DualLocal-1.0.9.zip`
  - SHA256 `9ab37d2654143e0adb175cd911ef15fb2ca2bf831e45a18de37bdbde4c8995c6`

退出旧程序再启动新版。公网地址和存档位置沿用既有配置；与此前测试包一样，EXE 暂无 Authenticode 签名。
