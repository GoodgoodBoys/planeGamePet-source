# Plane Pet Windows 更新发布

更新文件只使用 OSS 的独立对象前缀 `plane-pet/windows/stable/`，不得覆盖或删除
Bucket 中其他目录、固件对象或现有配置。

发布顺序：

1. 运行 `build.ps1 -BuildPublic` 并完成所有验证。
2. 运行 `New-PlanePetUpdateManifest.ps1 -Version x.y.z`。签名私钥默认只从
   `%LOCALAPPDATA%\PlanePetRelease\update-signing-private.csp` 读取，不得提交到 Git
   或上传 OSS。
3. 先上传 `versions/x.y.z/PlanePet.exe`，确认其 SHA-256 和长度与清单一致。
4. 再上传 `latest.json.sig`，最后上传 `latest.json`。把清单放在最后可避免客户端
   在发布窗口读到尚未齐备的版本。
5. 分别检查三个对象的 HTTPS 地址，然后用右键菜单“检查软件更新…”做实际验证。

`minimum_supported_version` 只用于确实无法继续兼容的机制或服务端协议变化。普通
功能和视觉更新不要提高这个值。版本采用 `主版本.次版本.修订版本`；大版本不同的
已绑定双方会暂停邀请和表情等联机动作，旧版一方升级后自动恢复。
