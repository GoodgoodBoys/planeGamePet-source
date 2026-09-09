# Plane Pet Windows 更新发布

正式版更新文件只使用 OSS 的独立对象前缀 `plane-pet/windows/release/`，不得覆盖或删除
Bucket 中其他目录、固件对象或现有配置。

`stable/` 保留为旧测试通道；不要把正式 `1.0.0` 写入该通道，更不要覆盖曾经发布的测试 `1.0.0`。
正式版本按 `(release_epoch=1, 主版本, 次版本, 修订版本)` 识别，旧测试版属于 epoch 0。
旧版用户已选择一次手动迁移，替换程序前正常退出，保留 `%LOCALAPPDATA%\PlanePet`。之后由正式通道正常升级。

发布顺序：

1. 测试候选用 `build.ps1 -BuildPublic`。正式签名构建增加 `-RequireSignature -SigningCertificateThumbprint <当前用户证书指纹> -TimestampUrl <批准的时间戳地址> -SignTool <Windows SDK signtool路径>`。构建先签组件，再嵌入并签启动器，最后打包/计算摘要。没有有效证书和私钥、时间戳或签名验证失败时停止。完成 `RELEASE-ACCEPTANCE.md` 验收。
2. 运行 `New-PlanePetUpdateManifest.ps1 -Version x.y.z`。本次开发者明确暂缓 Windows 发布者签名，须额外显式传入 `-AllowUnsigned`；其他无效签名仍拒绝。入口检查版本、最低版本、单端组件、正式通道标记、摘要及同版本不可变性。签名私钥默认只从
   `%LOCALAPPDATA%\PlanePetRelease\update-signing-private.csp` 读取，不得提交到 Git
   或上传 OSS。
3. 先上传 `versions/x.y.z/PlanePet.exe`、完整 ZIP、`release.json` 和 `release.json.sig`，确认 EXE 的 SHA-256 和长度与清单一致。同版本不可覆盖不同内容；源文件和元数据先冻结再上传。
4. 再上传 `latest.json.sig`，最后上传 `latest.json`。两个对象不能原子切换，仍有短暂签名不匹配窗口；客户端必须拒绝并重试，不可宣称此顺序完全消除了窗口。保留旧清单/签名以便排查。
5. 分别检查三个对象的 HTTPS 地址，然后用右键“帮助与更新 → 检查软件更新…”做实际验证（1.0.1/1.0.2 旧版入口仍在一级菜单）。

`minimum_supported_version` 只用于确实无法继续兼容的机制或服务端协议变化。普通
功能和视觉更新不要提高这个值。版本采用 `主版本.次版本.修订版本`；大版本不同的
已绑定双方会暂停不兼容的联机动作，旧版一方升级后自动恢复。正式/测试 epoch 不同也会阻断对战，不能只按数字比较。

验收至少运行 `verify_fix_regressions.ps1`、`verify_update_windows.ps1`、`verify_release_hardening.ps1`、`tests/verify_embedded_components.ps1`、`tests/verify_release_preflight.ps1`，以及真实更新代码的离线 RSA/哈希/错误通道校验和发布后的线上清单检查。
`.publish.lock` 只用于本地串行发布，不上传 OSS。签名私钥、用户档案、后台数据、SSH 密钥绝不进入发行目录或 Git。
