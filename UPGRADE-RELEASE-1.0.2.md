# 1.0.2 单端公网升级发布核查

日期：2026-09-06。用途：让用户从现有 1.0.1 单端公网包体验真实升级。

## 发布范围

- `common/app_version.h` 从本次工作开始时的 1.0.1 提升为 1.0.2。
- 本次是升级链路验证用的非强制修订版，沿用 1.0.1 功能修复，没有新增玩法。
- `minimum_supported_version=1.0.0`、主版本 1、清单 schema 1；不迁移绑定、战绩、设置或统计结构。
- 沿用已有 RSA-2048 更新签名密钥。签名私钥没有上传、替换或放入仓库。
- 只上传以下三个 OSS 对象，顺序为程序、签名、最终清单：
  - `plane-pet/windows/stable/versions/1.0.2/PlanePet.exe`
  - `plane-pet/windows/stable/latest.json.sig`
  - `plane-pet/windows/stable/latest.json`
- 没有修改 ECS 服务/配置、FingerKnight、原 ESP32 场景、共享安全组、证书、Bucket 设置或其他 OSS 对象。
- 既有单端用户会读到这次可选更新，需要用户同意才安装。

## 工件

| 工件 | 大小 / SHA-256 |
|---|---|
| 已发布 PlanePet.exe | 14,757,458 字节；`3e5bc2bcc712e2e3238a9e7103aaf46587eb64573d2c6faecdfd5068dca2af4a` |
| 1.0.2 单端 zip | `06c87654fff40273965dfff0dd954fb377ddb36804f6237625d5210774b052b6` |
| 保留的 1.0.1 单端 zip | `8bfdb5f2fa607e8ff9ab852868a7080da1b15341a51b62bb38546a6e5837ea37`，与发布前相同 |
| latest.json | `ed05c47e5f434faaa35919ae5fbc31cf07667367a5c25325bbae8b4cde9b073d` |
| latest.json.sig | `176250806012a02dc5c5e033eca5c1e85e8cf1d99a6f630aeaef21a5b3d8649a` |

发布文件保存在 `release_out/v1.0.2/`。切换前的线上 1.0.0 清单和签名已备份到
`release_out/upgrade-validation-1.0.1-to-1.0.2/previous-live/`，可用于人工撤回发布。
程序自身的失败回滚另由更新器管理。

## 已通过的验证

1. `build.ps1 -BuildPublic`：完整编译，`PC_PET_BUILD_OK`。
2. `verify_fix_regressions.ps1`：客户端、游戏、服务器、21 项网关/统计测试、
   匹配生命周期、旧协议对局/平局、版本兼容和恢复绑定、更新器五类路径、包静态检查。
   结果 `PLANE_PET_CONFIRMED_FIX_REGRESSIONS_OK`。
3. `verify_update_windows.ps1`：状态测试及实际测试窗口的 X、边缘点击、Esc、隐藏、
   邀请/游戏暂缓、过期异步消息等，`PLANE_PET_UPDATE_WINDOWS_OK`。
4. 保留真实 1.0.1 更新代码及头文件，分别用它和 1.0.2 编译探针：
   新清单的实际嵌入式公钥验签、大小、SHA-256 均通过；篡改清单被拒绝。
5. 从 OSS 下载已发布程序，大小与哈希与本地签名清单完全相同。
6. 保留的真实 1.0.1 更新器，在唯一隔离目录替换 1.0.1 程序副本为公网下载的
   1.0.2，真实新版启动器释放客户端/隧道/更新器并回报健康，事务正常提交。
   合成未绑定身份、设置、三条战绩逐文件哈希保持不变，测试统计为关闭状态。
   `REAL_PUBLIC_UPGRADE_OK`；证据 `release_out/real-upgrade-d5e3e3e2/`。
   只结束隔离测试产生的进程，没有操作用户正在运行的双端包及实际存档。
7. 发布后使用真实 1.0.1 更新代码访问正式 HTTPS 入口并完成下载校验：
   `UPDATE_ONLINE_DOWNLOAD_OK client=1.0.1 latest=1.0.2 optional=1 notes=1 signed_request=1`。
8. 1.0.2 更新代码访问正式入口：
   `UPDATE_ONLINE_CURRENT_OK client=1.0.2 latest=1.0.2`。

日志在 `dist/build-1.0.2.log`、`dist/fix-regressions-1.0.2.log`、
`dist/update-windows-1.0.2.log`、`dist/real-public-upgrade-1.0.1-to-1.0.2.log`、
`dist/online-upgrade-probe-1.0.1.log` 和 `dist/online-upgrade-probe-1.0.2.log`。

测试维护：未来小版本测试改为从当前版本推导；包校验脚本默认读版本头文件；
旧公网检查测试不再把线上版本写死为 1.0.0。隔离测试第一次仅因人工输入 LF 与
Windows 存档 CRLF 的字节差异失败，内容相同；统一原生格式后复测通过。

## 用户手工体验步骤及边界

1. 从桌宠右键菜单退出正在运行的双端包，防止默认本机端口冲突。
2. 解压 `dist/PlanePet-Public-Single-1.0.1.zip` 到可写文件夹，双击其中的 `PlanePet.exe`。
   不要在压缩包内直接运行，不要选 `Public-DualLocal` 包。
3. 空闲时右键桌宠 → 检查软件更新，确认提示当前 1.0.1、最新 1.0.2 和更新说明。
4. 同意后等待下载、验证、替换与重启，再次检查更新应显示当前 1.0.2 已是最新。
5. 若要测试拒绝提醒，先点“暂不升级”；自动提示应暂停七天，手动检查仍可触发。
6. 在自己的真实绑定和战绩上确认升级后保留。这里未擅自替用户点击升级或覆盖真实存档；
   自动验证不等于已完成用户手工体验，也不承诺任意机器/网络环境绝对无故障。

本发布包没有 Windows Authenticode 代码签名；更新清单的 RSA 签名与 Windows 的
发布者签名是两件事。首次运行仍可能遇到 SmartScreen/安全软件提示，不应关闭系统防护。
