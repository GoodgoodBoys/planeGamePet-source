# 3.0.5 击中反馈与对称状态栏验收

日期：2026-09-08。范围：Windows PC→PC 客户端表现层；不修改 ESP32 场景、服务器配置、FingerKnight 或联网/碰撞算法。未上传 GitHub、OSS，也未更改更新清单。

## 交付内容

- C 橙金像素斜体 HIT!：显示在击中者自己的血条与中部计时之间；突然放大、双平行亮带扫过、轻微缩小、淡出。
- 爱心碎裂：复用既有 A 组红蓝爱心；顶部向下快速开裂、沿同一条锯齿切口分成两半；0.20–0.50 秒保持分开姿态，随后快速下落渐隐。灰色槽固定不动。
- C 受损涂装：2 血局部弹痕，1 血更明显弹痕；红蓝飞机均实现，战场与顶部小飞机随血量变化，桌宠和历史飞机不受影响。
- HUD 镜像排版：飞机在两端、爱心靠内、HIT! 再靠中央，计时与延迟居中。窗口仍为 240×372 逻辑像素，独立 HUD 52，高度 320 的完整地图不变。
- 动画由游戏显示用的插值血量驱动；不会提前读取未显示的碰撞结果。重复帧/回滚不反复播放，同步失败、掉线和平局不产生虚假 HIT!。下一局清空状态。原有 WIN!/LOST! 立即出现与爆炸播完后返回行为保留。

## 检查结果

| 验收 | 结果 |
| --- | --- |
| `verify_effects.ps1` | 严格编译及效果模型测试通过；真实生产渲染路径验证 4 个受损模型、76 个爱心帧、44 个 HIT! 帧；第五阶段帧逐像素一致、双方视角归属正确、动画过期/新局重置/提前碰撞门控通过 |
| 原有特效回归 | 拖尾、烟雾、命中爆炸、终局爆炸、结果立即显示、双方请求返回时播完爆炸、稳定 GDI 资源通过 |
| `build.ps1 -BuildPublic` | 完整客户端、服务端、隧道、升级器及单端/单机双端公网包编译通过 |
| `verify_release_hardening.ps1` | 正常 Windows 用户环境 702 PASS / 0 FAIL；含窗口、托盘、鼠标工具栏、帮助、战绩、菜单、100%/125%/150%/200% DPI |
| `verify_game_render.ps1` | 240×372 / HUD52 / 地图320；双方边界与桌宠追逐 4×120 秒检查通过 |
| `verify_pet_visual_assets.ps1` | 既有 D2 红蓝飞机、三组云、43 帧勾引表情资源通过 |
| `verify_battle.ps1 -AllowPublicTest` 本地部分 | 移动 777476 项、战斗 1320309 项、真实渲染 54 帧；200ms/2% 丢包正常结束、300ms/5% 丢包中断、游戏中及倒计时 7 秒断流恢复检查通过 |
| 正常用户环境公网正常对局 | TLS 隧道两个真实客户端完成对局，双方最终 HP 和结果一致 |
| 正常用户环境公网断线结束 | 双方明确结束，不伪造击中，不计入普通对战战绩 |
| 公网测试隔离 | 两次均确认原有资料未改变、临时凭据副本删除、测试绑定清理；统计关闭；未调整服务器配置 |
| 公网包静态验证 | Single、DualLocal 两包通过；客户端、隧道、升级器嵌入 SHA-256 与当前编译产物相同 |
| 新素材嵌入验证 | 资源 174–178 与源 PNG 哈希完全一致；旧爱心、拳头仍与批准素材一致 |
| 改动文件检查 | 针对本次受影响的已跟踪文件 `git diff --check` 通过 |

说明：第一次受限环境的窗口验收为 699/702，未通过的是托盘重建和真实鼠标检查；切换正常用户桌面重跑后全部通过。首次公网验收在受限用户环境无法解密现有 QA 凭据，未连接服务器；正常用户环境的隔离验收已通过。未为消除环境问题而削弱测试或修改相关旧逻辑。

## 原始记录

- 窗口：`dist/release-regression-40b6a3ad66a144339a33a06036dedf5a/中文 用户路径/检查结果.txt`
- 实际新效果渲染：`dist/vfx-verification-3.0.5/feedback-v*-target*-*.bmp`
- 动图预览：`dist/vfx-verification-3.0.5/runtime-combat-feedback.gif`（生产渲染帧放大两倍，实际窗口尺寸没有放大）
- 本地战斗：`dist/battle-verification-41276c9c41d14f7da0c877df940ea13f`
- 公网对局：`dist/motion-live-eb161ab5d0d146ff8f6d717ad14ae785/result.json`
- 公网对局资料隔离：`dist/motion-public-9cbb9f1d6b4b4c3b94c74a4652f8837a/safety.json`
- 公网中断：`dist/motion-live-3f152efb38574348a9ee14783d9e484e/result.json`
- 公网中断资料隔离：`dist/motion-public-f24bd283910d4986928b81f517c97f43/safety.json`

## 发布文件

- 单端：`dist/PlanePet-Public-Single-3.0.5/PlanePet.exe`
- 单端包：`dist/PlanePet-Public-Single-3.0.5.zip`
  - SHA-256：`cc0e409fa2d3bd64232e5f7bf0527307858e7bfe49415eec5bbb040c24d642a7`
- 单机双端公网包：`dist/PlanePet-Public-DualLocal-3.0.5.zip`
  - SHA-256：`c13301d3f3577b436a4e687322d7ec23b2cc9af495b5b725bd8ae89c0040c116`

仍是未代码签名的测试构建，不能据此声称所有 Windows 机器上绝无兼容问题。这里记录的是已执行并通过的检查；没有将测试报告当作零缺陷保证。

## 物理与旧素材保护

以下文件在改动前后的 SHA-256 完全相同：

- `common/pc_battle.h`：`5283BFA2CC35E7EE42A59143A3CBA9CA042FA1BBC373FCD90536B937E86F0B5E`
- `common/pc_battle_view.h`：`901A0D299F73454A9483B94CA9DB35F0D05715C2FEA84068F392508BB869E2F2`
- `common/pc_motion.h`：`3112C72985CC6F07F81A13C8BCE0E60FDB86F77476DB932F014AA8804AEED06D`
- `server/main.cpp`：`57A995889CA6C21185B75A2DBAEE4D67762431E0AE212DA805636B8476EFC0E0`
- `assets/planes/pet-blue-d2.png`：`B5E6B719B29E6C343B08428BB1224AB38DA5145E2F4EC9371708F19A197D009C`
- `assets/planes/pet-red-d2.png`：`E50B296C5820D7D85925759C54E8CEC312FE3911FA0EFCDB30E5C70841C15A91`
