# 1.0.2 候选版：随机追逐与枪口跟踪
2026-09-09。仅修改 Idle 演出及相应验证；不是正式发布，也不包含上一轮出厂审查中列出的存储/运维问题修复。

## 行为
- 去除闭合环形曲线。红机不定时选择新的随机目的地、速度和逃跑方向；新目标约每 2.2–5.8 秒刷新，抵达及换边动作也会触发目标调整。
- 红机平滑转向，蓝机追近并保持间距。窄窗口遇到互相堵路时，蓝机会让出另一条窄通道，让红机穿插换边。让位时蓝机可短暂横移/后退，枪口仍跟踪红机。
- 蓝机的朝向直接依据当前插值后的红机位置计算。开火仍保留原来的 ±15° 随机散射；子弹只在出生时确定方向，不会追踪红机，无伤害或击落。
- 窗口依然为 280×150；上一轮的“开一局＋四个表情”悬停行保留，自动开火频率、云、游戏大小和真实战斗规则不改。

## 实现与保护
使用每 16ms 一步的本地状态模拟，而不是在每次重绘时重新抽随机数。每次启动使用独立随机种子；同一种子与输入时刻可重复验证。
保留 128 帧历史，供现有子弹重建取回出生时枪口。绘制、悬停命中、表情及状态徽标共用同一模拟。
后台停顿过长或 UI 时间回绕时重置时间基准、保留当前姿态，不回放海量漏帧。飞机间有软让位和最后的最小距离约束。

## 本轮验收
- 997 种种子 × 5 分钟、16ms 采样：两机全方向、左右两种转向、边界、机距及枪口目标一致性全部通过。抽样机距 52.991–133.588px；单步最大 0.7713px，最大机头转角约 2.20°。
- 30/60/144 FPS 同时刻轨迹一致；重复绘制不重抽随机数；1120ms 子弹历史不回退模拟；已发子弹直线方向不变。
- 隔离的一天时间跳跃与 UI 计数回绕测试通过；仅是模拟时钟输入，不代表真实待机一天验收。
- 实际 Windows 窗口 784 PASS / 0 FAIL，含四档 DPI、拖动、按钮、邀请、勿扰、历史、帮助、关于和状态图标。
- 客户端/服务器/网关/版本门禁、旧存档迁移、旧设备场景、更新器与单端/双端静态回归通过。
- 完整构建成功，内嵌客户端、TLS 隧道、升级器、素材、隐私和许可摘要一致；帮助 18 章同步检查及发行防错预检通过。
- 查看真实程序输出的 0、3.75、7.5、11.25 秒关键帧，确认换边后蓝机朝向红机，气泡、状态点、工具栏未相互挤占。

记录：[核心回归](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/regressions-1.0.2-chase.log)、[窗口回归](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/windows-1.0.2-chase.log)、[构建](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/build-1.0.2-chase.log)。

## 交付
- [单端公网 EXE](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/PlanePet-Release-Single-1.0.2/PlanePet.exe)
- [单端公网 ZIP](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/PlanePet-Release-Single-1.0.2.zip)
- [单机双端公网 ZIP](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/dist/PlanePet-Release-DualLocal-1.0.2.zip)

先退出旧程序，再运行新版；不要同时启动单端和双端。没有修改 ECS、FingerKnight、真实存档或生产配置，也没有推送 GitHub/OSS。1.0.1 交付 EXE 摘要仍为 fe28170e88133dd09fc7181a4258bec17e650e54eddc97145e1d71eecb3e46e6。

新单端 EXE SHA-256：8d3f4ad50f7c443d0110a7067f6199f1ad6a1ed67a3b997c58fe1c1ec3dcf1e0。
新单端 ZIP SHA-256：431bb25e5bcd03962953bd3569ba805eeed79920e1f528ca949721e778d464c5。
新双端 ZIP SHA-256：6cbd455bf6537b575279f1e8649b937106b391069088d68bc02ba6a97f17945f。

本轮结果仅覆盖上述改动和验收；[1.0.1 出厂审查](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/REVIEW-1.0.1.md)列出的未修复事项仍有效。
