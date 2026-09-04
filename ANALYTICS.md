# Plane Pet 匿名测试统计后台

## 访问

- 公网地址：`https://8.166.124.212/admin`
- 用户名：`plane-pet`
- 密码只保存在 ECS：`/etc/plane-pet/gateway-admin.password`
- 页面每 60 秒刷新，必须通过有效 TLS 和 HTTP Basic 鉴权访问。

## 采集范围

只有用户明确同意后，公网客户端才会发送：

- 程序启动、正常退出和每 60 秒一次的会话心跳；
- 匹配开始、停止和成功；
- 邀请发出、接受、拒绝、取消和超时；
- 对局开始、结束、用时、胜负及结束原因；
- 四种固定快捷表情的发送类型与好友端接收次数；
- 桌宠显示、隐藏和勿扰状态变化。

不采集聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容、窗口标题或其他应用信息。匿名安装标识由
随机隧道凭据的 SHA-256 哈希截取生成，不能直接还原为凭据。网关只持久化用于防滥用限流的
不可逆来源分组值，不保存原始来源 IP；公网隧道和健康检查关闭 Nginx 访问日志。

## 数据与指标

- SQLite：`/var/lib/plane-pet/telemetry.db`
- 原始事件默认保留 90 天；清理在下一条事件写入时每日最多执行一次。
- “启动会话”以 `app_started` 计数。
- 使用时长优先采用正常退出上报时长；异常退出采用最后事件与启动事件的时间差估算。
- 邀请和对局以服务端下发的匿名 `invite_id` / `round_id` 去重，双方上报不会重复算一局。
- “当前活跃”指最近 120 秒内上报过事件或心跳、且同意统计的匿名安装。
- “快捷表情”独立展示发送总数、好友端收到总数、使用人数和四类表情分布。

## 运维

查看状态：

```bash
systemctl status plane-pet-gateway.service
journalctl -u plane-pet-gateway.service --since today
```

轮换管理员密码：

```bash
openssl rand -out /etc/plane-pet/gateway-admin.password -hex 24
chown root:plane-pet /etc/plane-pet/gateway-admin.password
chmod 0640 /etc/plane-pet/gateway-admin.password
systemctl restart plane-pet-gateway.service
```

备份数据库时使用 SQLite 在线备份命令或先停止网关，避免只复制主数据库而遗漏 WAL。
服务器配置和旧二进制的部署前备份保存在 `/var/backups/plane-pet` 或原文件旁的
`.pre-analytics-20260902` 文件中。
