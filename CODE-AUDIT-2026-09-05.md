# Plane Pet 全面代码审查 — 2026-09-05

## 范围与结论

针对当前 PC → PC 应用，检查 Windows 桌宠、游戏渲染与输入、匹配服务器、共享协议与模拟、单端/双端启动器、公网隧道、升级器、统计网关、部署配置和帮助内容。基线为源码仓库 904cdd2 加当前工作区已有的修复；没有回退或覆盖这些修改。

本轮只做审查及隔离诊断，没有修改业务源码、发布包、GitHub、OSS、公网服务配置、真实绑定或统计数据。原 ESP32 ↔ Windows 场景不在本轮逐文件审计范围，也没有改动。服务器配置结论来自仓库文件，**未声称已核对 ECS 当前生效配置**。

除前一轮已经说明的更新窗口关闭问题及原生模态菜单/对话框阻塞客户端主循环的问题外，确认了以下问题。P1 建议在继续扩大发放测试前解决，P2 应进入下一轮修复；这不是“程序完全不可用”，正常流程测试仍能通过。

## P1：优先修复

### F01 — 停止匹配与匹配成功竞争，可能留下无法使用的永久绑定

位置：[desktop/main.cpp:1779](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:1779)、[desktop/main.cpp:2019](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2019)、[server/main.cpp:595](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:595)、[server/main.cpp:616](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:616)。

服务器收到第二人的 Start 后立即存下永久绑定；客户端此时点击停止会清空 requestId 并忽略稍后到达的 Matched。服务器处理 Cancel 只删等待队列，仍回复 Cancelled，不处理刚刚生成、尚未被客户端确认的绑定。客户端重新输入时得到 AlreadyBound，却没有凭据恢复这段绑定。

**隔离复现：** 服务器先完成双方绑定，A 尚未消费成功通知就取消；Cancel 获得确认，A 下一次 Start 返回 AlreadyBound。退出进程也存在同类窗口。

建议：明确“匹配提交/确认/取消”的协议状态，保存待确认请求与恢复路径；Cancel 必须给出实际终态，不能将已经提交的绑定当成普通等待记录处理。

### F02 — 旧的接受操作跨邀请重试，可能未经本次点击就接受下一次邀请

位置：[desktop/main.cpp:446](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:446)、[desktop/main.cpp:1944](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:1944)、[desktop/main.cpp:2238](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2238)、[server/main.cpp:918](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:918)。

Accept 只在收到 Countdown 时清除。若邀请人先取消、接受人随后发送 Accept，客户端收到 Menu 仍保留 pendingAction，每 250 ms 重发。下一次进入 Waiting，服务器仅判断动作和当前阶段，不校验 inviteId，因此会把旧 Accept 当作新邀请的接受。同时 IsUpdateUiAvailable 要求 pendingAction 为零，残留操作也会影响更新提示。

**隔离复现：** 使用真实客户端收包代码，Waiting → Menu → 新 Waiting 后 pendingAction 仍为 Accept。动作报文没有邀请/对局标识。

建议：动作携带 inviteId/roundId 与幂等操作 ID；离开对应阶段立即撤销无效重试，并以明确确认或拒绝完成动作。

### F03 — 迟到的解绑/错误通知可能清掉新绑定

位置：[server/main.cpp:448](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:448)、[server/main.cpp:698](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:698)、[desktop/main.cpp:2065](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2065)、[desktop/main.cpp:2095](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2095)。

部分 SendStatus 回复不带 bindingId；客户端将 bindingId=0 的 Unbound 当成可清除任意当前绑定的通知。BindingMissing、Invalid、AlreadyBound、StorageError 也缺少充分的当前请求关联校验。

**隔离复现：** 客户端已有 bindingId=222，注入来自受信服务器端点语义的旧请求 Unbound（bindingId=0），实际处理函数把现有绑定清为零。该测试模拟迟到响应，不代表外网可以无凭据直接向客户端注入报文。

建议：错误和解绑响应回填关联标识；校验当前操作代次及绑定 ID。服务端主动通知也应携带明确绑定 ID。

### F04 — 程序与用户数据不在同一磁盘时，升级替换失败

位置：[desktop/update_manager.cpp:646](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/update_manager.cpp:646)、[public_launcher/main.cpp:63](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/public_launcher/main.cpp:63)、[updater/main.cpp:210](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/updater/main.cpp:210)。

下载位于用户数据目录，目标是用户解压的 PlanePet.exe。用户数据在 C 盘、程序在 D 盘时，当前 MoveFileExW 仅使用 MOVEFILE_WRITE_THROUGH，不能跨卷搬运。微软明确要求跨卷时设置 MOVEFILE_COPY_ALLOWED；当前失败分支会恢复旧文件并退出，但不会重新打开应用。[微软 API 文档](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)

**静态确认；未对真实跨盘安装目录执行写入测试。**

建议：先将已验证的新文件复制到目标程序同目录的临时文件，再校验并执行同卷替换。不要把允许跨卷复制本身当成完整的原子升级方案。

### F05 — 统计写入阻塞整个公网转发，统计异常也未完全隔离

位置：[gateway/plane_pet_gateway.py:145](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:145)、[gateway/plane_pet_gateway.py:209](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:209)、[gateway/plane_pet_gateway.py:501](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:501)、[gateway/plane_pet_gateway.py:561](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:561)。

游戏 WebSocket 转发、SQLite 写入和后台汇总共用一个 asyncio 线程，统计每条事件同步提交。数据库被另一个写事务锁住时，会阻塞所有连接的处理；服务器对局掉线判定为 5 秒。此外，异常统计字段 e=[] 会在集合成员校验处抛 TypeError，经 bridge 传播后关闭当前游戏隧道，而非只丢弃统计事件。

**隔离复现：** 临时 SQLite 写锁使本应 20 ms 执行的事件循环心跳推迟约 5.5 秒；错误事件字段抛出 TypeError。未锁定生产数据库。

建议：统计通过有界队列交给独立数据库工作线程；汇总也与转发解耦。写入超时、格式错误或队列拥堵只能影响统计，不能结束游戏连接。

### F06 — 注册限制存在信任边界缺口，非法握手也会永久占用名额

位置：[deploy/nginx-plane-pet.conf:84](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/deploy/nginx-plane-pet.conf:84)、[gateway/plane_pet_gateway.py:548](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:548)、[gateway/plane_pet_gateway.py:571](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:571)、[gateway/plane_pet_gateway.py:587](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:587)。

Nginx 追加 X-Forwarded-For，后台却取最左侧值作为可信来源；请求者自行提供的首项因此可改变注册分组。新凭据在验证 WebSocket 握手前已存档，即使随后返回 400，也消耗总量名额。默认总名额 1000，没有正常过期回收流程。

**隔离复现：** 同一来源连续 8 个不完整升级请求消耗 8 个名额，第 9 个被拒；只改变转发头首项，即可绕过耗尽的来源分组继续登记。测试只调用本地导入的网关函数和临时文件，没有向公网发送这类请求。

建议：入口覆盖可信来源头，后端仅信任受控代理；先完成所有请求校验，再事务化登记；区分可撤销凭据、临时注册速率和总量治理。

## P2：功能与测试完整性

### F07 — 当前限制容易误伤同一办公室的正常用户

位置：[deploy/nginx-plane-pet.conf:76](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/deploy/nginx-plane-pet.conf:76)、[gateway/plane_pet_gateway.py:575](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:575)、[server/main.cpp:802](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:802)。

仓库 Nginx 配置每公网 IP 最多 4 条隧道，即同一办公室第 5 个单端用户可能被拒；同出口累计登记 8 个凭据后，退出或关闭旧设备不释放名额。双端测试包每次占两条隧道、两个凭据。另一个独立瓶颈是所有网关转发到游戏服务器都来自 127.0.0.1，而服务器按源 IP 总计只允许每秒 40 个控制包；约 20 个同时搜索且每 500 ms 重试的客户端即可用满配额。

**隔离复现：** 45 个不同本地 UDP 端点发送请求，只有 40 个得到响应。Nginx 的 4 条限制是配置检查结论，未在 ECS 上做并发压测。

建议：按经过认证的安装/连接做公平限流，保留较宽的出口 IP 兜底；注册速率采用时间窗口而不是终身累计次数，并按预期办公室规模验证。

### F08 — 好友从较新小版本升级到大版本后，强制提醒可能漏掉

位置：[desktop/update_manager.cpp:449](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/update_manager.cpp:449)。

peerRequirementHandled 是单个布尔值，仅在 peerVersionDiffers 变 false 时重置。若本机 1.0.0 已处理好友 1.0.1 的提醒，好友之后变为 2.0.0，差异一直存在，新的大版本要求不会触发检查。双方仍会被禁止邀请，但自动升级提醒漏发；手动检查仍可使用。

**隔离复现：** 将“之前小版本差异已处理”作为合法前置状态，Tick 收到新的大版本不兼容标志，快照代次不变，也没有 required 提示。

建议：以好友版本元组和兼容要求作为已处理键；从可选变为强制时立即升级提醒优先级，不受之前小版本提醒或 7 天免提醒状态影响。

### F09 — 接受/拒绝/取消按钮下沿有部分点击无效

位置：[desktop/main.cpp:664](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:664)、[desktop/main.cpp:675](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:675)、[desktop/main.cpp:1915](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:1915)。

无论当前是否显示表情选择，代码都先检测表情矩形并 return。表情区域从 y=116 开始，覆盖接受/拒绝按钮到 y=121 的一部分，也覆盖取消按钮下沿的部分区域。

**隔离复现：** 接受按钮内部 (90,118) 无动作，中心 (100,105) 正常排队 Accept。

建议：只在表情实际显示且可交互的状态命中表情；绘制和交互共享同一份状态与布局定义。

### F10 — 设置/清除数据失败仍显示成功，重启可能恢复用户已关闭的统计

位置：[desktop/main.cpp:921](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:921)、[desktop/main.cpp:967](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:967)、[desktop/main.cpp:1493](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:1493)。

SaveSettings 返回值没有被相关入口处理。用户关闭统计后，若文件被占用、目录不可写或磁盘错误，本次内存状态已关闭，但旧的“允许上传”配置仍在磁盘，下次启动重新加载。清除本地数据也忽略文件删除错误及战绩保存失败，仍显示“已清除”。7 天免提醒、勿扰等设置同样受保存失败影响。

**隔离复现：** 锁住临时设置文件，关闭统计的保存返回 false，重新 LoadSettings 后再次得到允许上传。没有操作用户真实设置。

建议：设置操作检查持久化结果；隐私选择保存失败必须明确告知，不能声称跨启动已生效。清除数据逐项检查，失败时保留可重试状态并报告未删除内容。

### F11 — 倒数时断线会把等待时间或上一局时间算进对局统计

位置：[server/main.cpp:320](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:320)、[server/main.cpp:918](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:918)、[server/main.cpp:1099](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/server/main.cpp:1099)、[desktop/main.cpp:2290](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2290)、[gateway/plane_pet_gateway.py:280](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:280)。

matchStarted 在创建房间或进入 Playing 时赋值，在新 Countdown 时没有重置。倒数中断线进入 Finished 后，从旧 matchStarted 计算时长，会混入本次开局前的 Idle 或上一局时间。客户端上报 game_finished，但这局没有 game_started，后台“完成/开始”的分子、分母也不一致，可能超过 100%。

**隔离复现：** 新房间先 Idle 1.3 秒，接受后立即在 Countdown 断开，未进入 Playing，返回的 matchElapsedMs 仍约 1432 ms；等待越久，误计越大。

建议：定义倒数中断是“未开局终止”还是已开局对局，分别记录；新轮次重置计时，并让开始数、完成数及平均时长使用一致口径。

### F12 — 中途开启统计时，会把尚未同意的运行时长也算进去

位置：[desktop/main.cpp:937](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:937)、[desktop/main.cpp:358](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:358)、[gateway/plane_pet_gateway.py:268](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:268)。

开启统计没有记录独立的同意起始时刻；正常退出上报的是从进程启动起的总时长。后台取退出上报与观察时间的较大值。例如先关闭统计运行 59 分钟，再开启 1 分钟并退出，后台可能计成 60 分钟。关闭后再开启也会把中间停止记录的时间计入。

**隔离复现：** 两条有效事件相隔 60 秒，退出值为进程总运行 3600 秒，后台累计使用返回 3600 秒。

建议：统计使用“同意期间”的独立活动段或累计启用时长，关闭即结束活动段，重开创建新段。正常使用持续超过 24 小时的会话还会被当前后台截短，需要一并统一时长口径。

### F13 — “原始事件最长保留 90 天”的承诺没有严格实现

位置：[gateway/plane_pet_gateway.py:229](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:229)、[gateway/plane_pet_gateway.py:620](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/gateway/plane_pet_gateway.py:620)、[desktop/main.cpp:630](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:630)。

过期清理仅由新事件写入触发，且一天最多一次。停止测试、没人上传时，过期数据会一直留存；打开后台或重启服务不会主动清理。帮助和首次同意说明写的是最长 90 天，当前实现并不满足。

**隔离复现：** 保存一条事件后，将后台查询时间推进 91 天且不再写入新事件，过期记录仍被统计并可显示。

建议：服务启动及独立定时任务清理；读取时排除过期记录作为兜底；说明清理周期及备份保留范围。

### F14 — 升级早期失败没有完整恢复和反馈，失败统计也存在断点

位置：[updater/main.cpp:180](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/updater/main.cpp:180)、[updater/main.cpp:195](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/updater/main.cpp:195)、[updater/main.cpp:206](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/updater/main.cpp:206)、[public_launcher/main.cpp:218](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/public_launcher/main.cpp:218)、[public_launcher/main.cpp:300](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/public_launcher/main.cpp:300)。

启动器已让原客户端退出后，升级器的路径、二次校验、备份、新文件搬运、状态写入等失败分支直接返回 2–6，只写 update.failure；只有“新版启动健康检查失败”才走重启旧版分支。源码中没有消费 update.failure 的客户端/启动器逻辑。启动器自己的失败重试带 --update-failed=1，但构造客户端参数时未转发它，因此对应失败统计也可能漏记。

**静态确认。** 当前成功/回滚测试主要覆盖健康检查失败，不覆盖这些早期分支。

建议：统一失败出口，在旧文件可用时恢复运行，显示明确失败原因并留出手动重试；下一次启动消费、展示并去重上报失败记录。

### F15 — 多显示器下，游戏窗口不会始终在桌宠附近打开

位置：[desktop/main.cpp:2497](C:/Users/81228/Desktop/alive/ProjCode/V1_1PFIO/plane_link/pc_pet/desktop/main.cpp:2497)。

游戏窗口用 SPI_GETWORKAREA 的主屏工作区限制位置，而帮助/升级窗口已使用 owner 所在显示器。桌宠放在副屏时，开局可能把游戏窗口钳制回主屏，违背“在悬浮窗附近打开”的交互要求。

**静态确认，未做多显示器实机操作。**

建议：游戏与其他窗口统一使用 MonitorFromWindow + GetMonitorInfoW，并覆盖负坐标、副屏及不同 DPI。

## 已做验证

- 8 个 C++ 入口/模块使用当前源码执行 -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only，全通过：desktop/main、update_manager、server、单端/双端/本机启动器、tunnel、updater。
- 新的客户端诊断直接编译包含当前 desktop/main.cpp，复现按钮下沿、残留 Accept、过期 Unbound、设置保存失败四类问题；不创建正常应用界面。
- 新升级状态诊断编译当前 update_manager.cpp，复现小版本差异转大版本差异的漏提醒。
- 本地隔离 UDP 服务器测试复现取消匹配竞态、倒数断线时长、回环地址共享限流。
- Python 临时数据库与模拟请求测试复现注册/转发头、数据库阻塞、错误事件字段、过期数据及同意期间时长问题。
- 现有升级状态 15 项测试通过；地图布局和双方移动/攻击对称性测试通过；网关统计单元测试 7/7 通过；git diff --check 通过（仅已有 LF/CRLF 提示）。
- 诊断脚本与编译产物位于 dist/review-20260905，未纳入发布包。临时测试不使用生产端口或真实存档；所启动本地测试服务器均已退出。

测试通过只证明覆盖到的路径，没有证明整个应用无缺陷。本轮没有做公网负载、真实跨卷安装、断电恢复、真实多屏 DPI、隧道长时间半开等故障注入。

## 建议修复顺序

1. F01–F03：先明确匹配提交、响应关联和邀请动作生命周期，避免修复一处竞态时制造另一处。
2. F04、F14：升级的跨卷暂存、统一失败恢复及可见反馈，再补强制结束/断电恢复测试。
3. F05–F07：统计与游戏转发隔离、可信来源和注册流程、适合办公室测试的限额。
4. F08–F10、F15：版本提醒、命中区域、保存结果、多屏窗口位置。
5. F11–F13：统一统计语义和保留策略，同时同步帮助与隐私说明；不要在未确认前擅自修正或清零已有生产统计。
