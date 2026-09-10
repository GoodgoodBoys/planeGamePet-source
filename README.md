# Plane Pet：Windows 双人联网桌宠

开发者：Ding · 反馈邮箱：planepet_public@163.com

## 首次公开发布：1.0.1

本仓库保存 Plane Pet 的源码、图片资源、构建脚本和回归测试。普通用户无需编译，请前往 [程序发布仓库](https://github.com/GoodgoodBoys/planeGamePet) 或 [1.0.1 正式发布页](https://github.com/GoodgoodBoys/planeGamePet/releases/tag/v1.0.1) 下载。

当前版本常量为 `1.0.1`，正式通道为 `release`，`release_epoch=1`。与现有发布程序对应的源码快照是 [`public-v1.0.1`](https://github.com/GoodgoodBoys/planeGamePet-source/tree/public-v1.0.1)。此后的发布说明整理不改变该标签，不重新编译现有 EXE，也不把其他候选版本改名为 1.0.1。

## 功能

- 六位匹配码绑定朋友，保存绑定、设置与战绩。
- 桌面飞机飞行展示，朋友在线、离线和勿扰状态提示。
- 鼠标移入后显示“开一局”和四个快捷表情按钮；右键菜单提供更多操作。
- 邀请、接受、拒绝、取消和超时反馈；接受邀请后打开对战窗口。
- 键盘或鼠标拖动控制飞机，自动射击，像素风受击与结束特效。
- 历史战绩、帮助、隐藏与退出，以及软件更新检测。
- 可自主选择的使用统计；拒绝或关闭可选统计不影响正常游戏。

操作与注意事项请阅读 [帮助](HELP.md)、[隐私与数据说明](PRIVACY.md) 和 [第三方许可](THIRD-PARTY-NOTICES.md)。

## 开发与构建

目标环境为 Windows 10/11 64 位，使用支持 C++17 的 MinGW-w64 工具链、`windres` 和 PowerShell。部分服务端与验收脚本需要 Python 3。工具路径按本机安装位置显式传入，例如：

```powershell
./build.ps1 -BuildPublic -Compiler 'C:\path\to\mingw64\bin\g++.exe' -ResourceCompiler 'C:\path\to\mingw64\bin\windres.exe'
```

当前单端构建输出为 `dist/PlanePet-Release-Single-1.0.1.zip`。构建会产生新的二进制文件；本地重新构建不代表获得与已发布 EXE 相同的文件哈希或下载信誉，不应覆盖已冻结的同版本发行文件。

目录说明：

| 目录 | 内容 |
| --- | --- |
| `desktop/`、`assets/` | 桌宠、对战界面与图片资源 |
| `common/`、`shared/` | 公共逻辑、版本和通信协议 |
| `public_launcher/`、`tunnel/`、`updater/` | 公网单端启动、联网与更新组件 |
| `server/`、`gateway/`、`deploy/` | Plane Pet 配套服务与部署工具 |
| `tests/`、`verify_*.ps1` | 回归测试与验收脚本，不是用户下载入口 |
| `release/` | 发布、签名、许可准备与校验工具 |

本仓库对应 Windows ↔ Windows 场景；不替换 ESP32 ↔ Windows 的其他项目代码。

## 验收与发布

发布流程见 [发布操作说明](release/README.md)，测试入口和历史验收限制见 [验收记录](RELEASE-ACCEPTANCE.md)。回归测试、双端测试工具及历史技术报告保留用于维护，报告中旧版本号不代表当前推荐下载版本，也不代表新候选版本已经上线。

当前正式下载入口只推荐 1.0.1。普通用户无需从旧测试版开始安装；已有用户迁移前正常退出程序，保留 `%LOCALAPPDATA%\PlanePet`，不要复制他人的绑定或联网凭据。

现有发布程序未做 Windows Authenticode 发布者签名；更新清单使用独立 RSA 签名与 SHA-256 校验。二者用途不同。不要为运行或验收程序关闭系统安全防护。

密钥、联网凭据、用户存档、后台数据和运行日志不得提交到源码仓库。第三方组件的许可不等同于对本项目全部源码和美术资源的统一授权，使用时请遵守相应权利和许可说明。
