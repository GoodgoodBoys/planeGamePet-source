param(
    [string]$Compiler = "C:\Installation\Lib\mingw64\bin\g++.exe",
    [string]$ResourceCompiler = "C:\Installation\Lib\mingw64\bin\windres.exe",
    [string]$PublicUrl = "wss://8.166.124.212:32112/v1/tunnel",
    [switch]$BuildPublic,
    [string]$SigningCertificateThumbprint = '',
    [string]$TimestampUrl = '',
    [string]$SignTool = 'signtool.exe',
    [switch]$RequireSignature
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$versionHeader = Get-Content -Raw -LiteralPath (Join-Path $root "common\app_version.h")
if ($versionHeader -notmatch 'kString\[\] = "(\d+\.\d+\.\d+)"') { throw "Missing app version" }
$appVersion = $Matches[1]
if ($RequireSignature -and [string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
    throw 'Formal distribution requires a code-signing certificate; unsigned builds cannot pass this gate.'
}
if ($SigningCertificateThumbprint -and -not $TimestampUrl) {
    throw 'Supply the approved RFC3161 TimestampUrl when signing.'
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "C++ compiler not found: $Compiler"
}
$compilerBin = Split-Path -Parent $Compiler
$versionTuple = ($appVersion -replace '\.', ',') + ',0'
Set-Content -LiteralPath (Join-Path $dist 'PlanePetVersion.rc') -Encoding ascii -Value @(
    '1 VERSIONINFO', "FILEVERSION $versionTuple", "PRODUCTVERSION $versionTuple",
    'FILEOS 0x40004', 'FILETYPE 0x1', 'BEGIN', ' BLOCK "StringFileInfo"', ' BEGIN',
    '  BLOCK "040904b0"', '  BEGIN', '   VALUE "ProductName", "Plane Pet\0"',
    "   VALUE `"FileVersion`", `"$appVersion\0`"", "   VALUE `"ProductVersion`", `"$appVersion\0`"",
    '   VALUE "FileDescription", "Plane Pet Windows\0"', '  END', ' END',
    ' BLOCK "VarFileInfo"', ' BEGIN', '  VALUE "Translation", 0x409, 1200', ' END', 'END'
)

$common = @(
    "-std=c++17", "-O2", "-Wall", "-Wextra",
    "-B$compilerBin\",
    "-static", "-static-libgcc", "-static-libstdc++"
)

function Build-One([string[]]$Arguments) {
    & $Compiler @common @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
    $outputIndex = [array]::IndexOf($Arguments, '-o')
    if ($SigningCertificateThumbprint -and $outputIndex -ge 0) {
        $binary = $Arguments[$outputIndex + 1]
        if ([IO.Path]::GetFileName($binary) -in @('PlanePetServer.exe',
            'PlanePetAlice.exe','PlanePetBob.exe','PlanePetClient.exe',
            'PlanePetTunnel.exe','PlanePetUpdater.exe','PlanePetFullFlow.exe',
            'PlanePetPublic.exe','PlanePetPublicDual.exe')) {
            & "$root\release\Sign-PlanePetBinary.ps1" -Executable $binary `
                -CertificateThumbprint $SigningCertificateThumbprint `
                -TimestampUrl $TimestampUrl -SignTool $SignTool
        }
    }
}

function Add-PackageNotices([string]$Folder) {
    foreach ($name in @('HELP.md', 'PRIVACY.md', 'THIRD-PARTY-NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $Folder $name) -Force
    }
    $licenseRoot = Join-Path (Split-Path -Parent $compilerBin) 'licenses'
    $destination = Join-Path $Folder 'licenses'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($component in @('gcc','mingw-w64','winpthreads')) {
        $source = Join-Path $licenseRoot $component
        if (-not (Test-Path -LiteralPath $source -PathType Container)) {
            throw "Missing toolchain license texts: $source"
        }
        Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $ResourceCompiler)) {
    throw "Resource compiler not found: $ResourceCompiler"
}
# Both the desktop reader and the single-file launcher need these texts. Prepare
# them before compiling resources, including non-public/local builds.
& (Join-Path $root 'release\Prepare-PlanePetNotices.ps1') -Compiler $Compiler
& (Join-Path $root "assets\generate_plane_icon.ps1")
$desktopResourceObject = Join-Path $dist "PlanePetClientResources.o"
Push-Location $root
try {
    & $ResourceCompiler (Join-Path $root "desktop\resources.rc") `
        -O coff -o $desktopResourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Desktop resource build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Build-One @(
    (Join-Path $root "server\main.cpp"),
    "-o", (Join-Path $dist "PlanePetServer.exe"),
    "-lws2_32", "-lbcrypt"
)

Build-One @(
    (Join-Path $root "desktop\main.cpp"),
    (Join-Path $root "desktop\update_manager.cpp"), $desktopResourceObject,
    "-o", (Join-Path $dist "PlanePetAlice.exe"),
    "-mwindows", "-lws2_32", "-lgdi32", "-lgdiplus", "-lole32",
    "-lshell32", "-lcomdlg32", "-lwinhttp", "-lcrypt32", "-lbcrypt"
)

# The two named binaries are kept for the one-PC demonstration.  This neutral
# binary is the distributable client used on either of two real computers.
Build-One @(
    (Join-Path $root "desktop\main.cpp"),
    (Join-Path $root "desktop\update_manager.cpp"), $desktopResourceObject,
    "-o", (Join-Path $dist "PlanePetClient.exe"),
    "-mwindows", "-lws2_32", "-lgdi32", "-lgdiplus", "-lole32",
    "-lshell32", "-lcomdlg32", "-lwinhttp", "-lcrypt32", "-lbcrypt"
)
Copy-Item -LiteralPath (Join-Path $dist "PlanePetClient.exe") `
    -Destination (Join-Path $dist "PlanePet.exe") -Force

$publicPackage = $null
$publicDualPackage = $null
if ($BuildPublic) {
    if ($PublicUrl -notmatch '^wss://[A-Za-z0-9.-]+(?::[0-9]{1,5})?/v1/tunnel$' -or
        ([uri]$PublicUrl).Port -gt 65535) { throw 'PublicUrl must be a trusted WSS /v1/tunnel endpoint without credentials, query or fragment.' }
    $publicConfiguration = Join-Path $dist 'PlanePetPublicConfiguration.h'
    Set-Content -LiteralPath $publicConfiguration -Encoding ascii -Value @(
        '#pragma once', ('#define PCPET_PUBLIC_URL L"' + $PublicUrl + '"'))
    Build-One @(
        '-include', $publicConfiguration,
        (Join-Path $root "tunnel\main.cpp"),
        "-o", (Join-Path $dist "PlanePetTunnel.exe"),
        "-mwindows", "-lwinhttp", "-lws2_32", "-lcrypt32", "-lbcrypt"
    )
    Build-One @(
        (Join-Path $root "updater\main.cpp"),
        "-o", (Join-Path $dist "PlanePetUpdater.exe"),
        "-mwindows", "-lbcrypt"
    )
    Build-One @(
        (Join-Path $root "tests\update_health_stub.cpp"),
        "-o", (Join-Path $dist "PlanePetUpdateHealthStub.exe"),
        "-mwindows"
    )
    Build-One @(
        (Join-Path $root "tests\update_unhealthy_stub.cpp"),
        "-o", (Join-Path $dist "PlanePetUpdateUnhealthyStub.exe"),
        "-mwindows"
    )
}

Build-One @(
    (Join-Path $root "tests\integration_test.cpp"),
    "-o", (Join-Path $dist "PlanePetIntegrationTest.exe"),
    "-lws2_32"
)

Build-One @(
    (Join-Path $root "tests\pairing_lifecycle_test.cpp"),
    "-o", (Join-Path $dist "PlanePetPairingLifecycleTest.exe"),
    "-lws2_32"
)

Build-One @(
    (Join-Path $root "tests\game_layout_test.cpp"),
    "-o", (Join-Path $dist "PlanePetGameLayoutTest.exe")
)

Build-One @(
    (Join-Path $root "tests\game_symmetry_test.cpp"),
    "-o", (Join-Path $dist "PlanePetGameSymmetryTest.exe")
)

Build-One @(
    (Join-Path $root "tests\update_client_test.cpp"),
    (Join-Path $root "desktop\update_manager.cpp"),
    "-o", (Join-Path $dist "PlanePetUpdateClientTest.exe"),
    "-lwinhttp", "-lcrypt32", "-lbcrypt"
)

Build-One @(
    (Join-Path $root "desktop\main.cpp"),
    (Join-Path $root "desktop\update_manager.cpp"), $desktopResourceObject,
    "-o", (Join-Path $dist "PlanePetBob.exe"),
    "-mwindows", "-lws2_32", "-lgdi32", "-lgdiplus", "-lole32",
    "-lshell32", "-lcomdlg32", "-lwinhttp", "-lcrypt32", "-lbcrypt"
)

$resourceObject = Join-Path $dist "PlanePetFullFlowResources.o"
Push-Location $root
try {
    & $ResourceCompiler (Join-Path $root "launcher\resources.rc") `
        -O coff -o $resourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Resource build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
Build-One @(
    (Join-Path $root "launcher\main.cpp"), $resourceObject,
    "-o", (Join-Path $dist "PlanePetFullFlow.exe"),
    "-mwindows"
)

if ($BuildPublic) {
    $publicResourceObject = Join-Path $dist "PlanePetPublicResources.o"
    Push-Location $root
    try {
        & $ResourceCompiler (Join-Path $root "public_launcher\resources.rc") `
            -O coff -o $publicResourceObject
        if ($LASTEXITCODE -ne 0) {
            throw "Public resource build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    Build-One @(
        (Join-Path $root "public_launcher\main.cpp"), $publicResourceObject,
        "-o", (Join-Path $dist "PlanePetPublic.exe"),
        "-mwindows", "-lshell32"
    )
    $publicFolder = Join-Path $dist "PlanePet-Release-Single-$appVersion"
    New-Item -ItemType Directory -Force -Path $publicFolder | Out-Null
    Copy-Item -LiteralPath (Join-Path $dist "PlanePetPublic.exe") `
        -Destination (Join-Path $publicFolder "PlanePet.exe") -Force
    $publicInstructions = @(
        "Plane Pet 单端公网正式版 v$appVersion（Windows 10/11 64 位）",
        '',
        '从历史测试版迁移：先从右键菜单退出旧程序，再运行本正式版。不要删除 %LOCALAPPDATA%\PlanePet，其中的绑定、设置和战绩继续保留。旧 1.x/2.x/3.x 测试版需手动替换一次；双方都改用正式版后恢复对战，以后通过正式通道正常升级。',
        '1. 将这个压缩包分别发给两位测试者；每台电脑各解压并运行一份。',
        '2. 双击 PlanePet.exe，无需安装、无需配置服务器，也无需允许防火墙 UDP 入站。',
        '3. 首次运行会询问是否允许发送匿名测试统计；选择“是”或“否”均可继续使用。',
        '4. 两位测试者输入任意相同的六位匹配码并按 Enter。输入框和匹配信息卡可按住左键拖动；右键取消勾选“显示绑定电脑框”可只看飞行动画，搜索中可按 Esc 或右键停止。',
        '5. 匹配成功会永久保存在本机；以后启动自动恢复，无需再次输入匹配码。',
        '6. 右键飞机可邀请对战、查看历史战绩、暂停接收邀请、隐藏或退出；解绑、统计与数据操作位于“设置与隐私”，操作帮助与更新位于“帮助与更新”。托盘菜单分组相同，隐藏后提供显示入口。',
        '勿扰：蓝机月牙表示自己勿扰，红机月牙表示好友勿扰；离线红点贴自己月牙右下边。只暂停接收新邀请，仍可主动邀请和发送表情。邀请勿扰好友时提示 3 秒，可取消或 Esc 关闭。',
        '7. 鼠标移到桌宠上，点击“开一局”直接邀请；点右侧 ◀ 展开四个表情，▶ 收起。移出后隐藏但保留展开，10 分钟未发送表情才收起。闲置时自动射击，方向随机偏转左右各 15°，左键不控制射击。双方在线时红蓝飞机继续跟随飞行。',
        '8. 邀请最长等待 5 分钟；对方接受后打开小型游戏窗口，最长对局 3 分钟。',
        '9. 游戏使用 WASD、方向键，或按住鼠标左键拖动飞机；结束后单击或按键返回桌宠。',
        '10. 隐藏桌宠后收到邀请会自动弹出。要彻底关闭程序，请右键飞机或托盘图标选择“退出”。',
        '11. 程序会安全检查新版本；非必要更新可选择 7 天后提醒。也可右键选择“帮助与更新 → 检查软件更新…”。大版本不兼容时，联机功能会暂停到旧版一方完成升级。',
        '',
        '联网说明：程序通过 TLS 加密连接测试服务器；匹配码只用于当次寻找正在等待的另一端。',
        '完整操作帮助：参阅随包 HELP.md，或右键打开“帮助与更新 → 操作帮助”，从简略帮助进入详细帮助。',
        '绑定存储：匹配成功后，本机和服务器保存随机设备标识及绑定令牌，用于下次自动恢复；不保存姓名，匹配码不会作为绑定凭据保存。',
        '联网鉴权：无论是否同意匿名统计，安装凭据都会通过 TLS 加密连接传输用于鉴权；服务器持久保存其不可逆哈希、登记时间和防滥用限流所需的来源网络信息，而非原始令牌。这类联网鉴权数据不属于可选使用统计。',
        '关于：右键菜单“关于…”可查看开发者 Ding 和当前版本，点击底部链接可离线阅读隐私说明和完整第三方许可。反馈邮箱：planepet_public@163.com。',
        '隐私说明：同意后发送启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天，可在右键菜单随时停止。',
        '使用限制：一台电脑只运行一个单端实例；请勿同时运行单机双端测试版，否则本地端口会冲突。',
        '补充统计范围：同意统计后也记录升级检查、用户选择、下载/安装和回滚结果。右键“帮助与更新 → 连接与诊断…”可查看本机联网状态，同组“导出诊断信息…”只导出脱敏状态，不会自动上传。统计开关在“设置与隐私 → 匿名使用统计”。再次打开单端 EXE 可唤回桌宠。详见随包 PRIVACY.md。',
        '此版本尚未配置 Windows 发布者签名，因此可能显示未知发布者；更新清单另有 RSA 签名与 SHA-256 校验，不等同于 Windows 发布者签名。'
    )
    Set-Content -LiteralPath (Join-Path $publicFolder "使用说明.txt") `
        -Value $publicInstructions -Encoding utf8
    $publicPackage = Join-Path $dist "PlanePet-Release-Single-$appVersion.zip"
    Add-PackageNotices $publicFolder
    Compress-Archive -Path (Join-Path $publicFolder "*") `
        -DestinationPath $publicPackage -Force

    $publicDualResourceObject = Join-Path $dist "PlanePetPublicDualResources.o"
    Push-Location $root
    try {
        & $ResourceCompiler (Join-Path $root "public_dual_launcher\resources.rc") `
            -O coff -o $publicDualResourceObject
        if ($LASTEXITCODE -ne 0) {
            throw "Public dual resource build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    Build-One @(
        (Join-Path $root "public_dual_launcher\main.cpp"),
        $publicDualResourceObject,
        "-o", (Join-Path $dist "PlanePetPublicDual.exe"),
        "-mwindows"
    )
    $publicDualFolder = Join-Path $dist "PlanePet-Release-DualLocal-$appVersion"
    New-Item -ItemType Directory -Force -Path $publicDualFolder | Out-Null
    Copy-Item -LiteralPath (Join-Path $dist "PlanePetPublicDual.exe") `
        -Destination (Join-Path $publicDualFolder "PlanePet.exe") -Force
    $publicDualInstructions = @(
        "Plane Pet 单机双端公网测试版 v$appVersion（Windows 10/11 64 位）",
        '',
        '1. 解压后双击 PlanePet.exe；程序会打开 A端、B端两个独立桌宠。',
        '2. 两端都通过 TLS 连接公网 ECS，不会启动本地游戏服务器。',
        '3. 首次运行时两个桌宠会分别询问是否发送匿名测试统计；选择“是”或“否”都可继续。',
        '4. 在 A端和 B端输入任意相同的六位匹配码，然后分别按 Enter；输入框和匹配信息卡可按住左键拖动，右键可切换“显示绑定电脑框”。',
        '5. 红机绿点表示好友在线，月牙表示勿扰；好友离线时蓝机显示红点。右键任一飞机选择“邀请对战”，在另一端点击“接受”。',
        '6. 鼠标移到桌宠上，点击“开一局”直接邀请；点右侧 ◀ 展开四个表情，▶ 收起。移出后隐藏但保留展开，10 分钟未发送表情才收起。闲置时自动射击，方向随机偏转左右各 15°，左键不控制射击。双方在线时可测试双向表情。',
        '7. 游戏使用 WASD、方向键，或按住鼠标左键拖动飞机。',
        '8. 结束时请分别右键两个桌宠并选择“退出”；启动器随后自动关闭两条公网隧道。',
        '',
        '重启验证：匹配成功后退出两个桌宠，再次双击 PlanePet.exe，应自动恢复永久绑定。',
        '完整操作帮助：参阅随包 HELP.md，或右键打开“帮助与更新 → 操作帮助”，从简略帮助进入详细帮助。',
        '说明：两端拥有独立设备身份、DPAPI 凭据、存档和本地 UDP 端口，但共享这台电脑的公网出口。',
        '绑定存储：匹配成功后，本机和服务器保存随机设备标识及绑定令牌，用于下次自动恢复；不保存姓名，匹配码不会作为绑定凭据保存。',
        '联网鉴权：无论是否同意匿名统计，安装凭据都会通过 TLS 加密连接传输用于鉴权；服务器持久保存其不可逆哈希、登记时间和防滥用限流所需的来源网络信息，而非原始令牌。这类联网鉴权数据不属于可选使用统计。',
        '关于：右键菜单“关于…”可查看开发者 Ding 和当前版本，点击底部链接可离线阅读隐私说明和完整第三方许可。反馈邮箱：planepet_public@163.com。',
        '隐私说明：同意后发送启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天，可在右键菜单随时停止。',
        '不要同时运行普通公网版或其他单机双端测试实例，否则本地端口可能冲突。',
        '菜单：右键常用项保留邀请、历史战绩、暂停邀请、隐藏和退出；统计、导出记录、清除本地战绩与统计、解除绑定在“设置与隐私”；帮助、更新和诊断在“帮助与更新”。托盘菜单分组相同，隐藏后可显示。',
        '诊断：右键“帮助与更新 → 连接与诊断…”区分本机网络异常与好友离线；同组可主动导出脱敏诊断信息。统计开关在“设置与隐私 → 匿名使用统计”。双端测试版暂不支持升级交接，检查更新仍置灰；单端版可正常检查。补充统计范围、反馈和数据说明见随包 PRIVACY.md。',
        '若 Windows SmartScreen 提示未知发布者，是因为测试构建尚未配置商业代码签名证书。'
    )
    Set-Content -LiteralPath (Join-Path $publicDualFolder "使用说明.txt") `
        -Value $publicDualInstructions -Encoding utf8
    $publicDualPackage = Join-Path $dist "PlanePet-Release-DualLocal-$appVersion.zip"
    Add-PackageNotices $publicDualFolder
    Compress-Archive -Path (Join-Path $publicDualFolder "*") `
        -DestinationPath $publicDualPackage -Force
}

$lanPackage = Join-Path $dist "PlanePet-LAN.zip"
Compress-Archive -LiteralPath @(
    (Join-Path $dist "PlanePetClient.exe"),
    (Join-Path $dist "PlanePetServer.exe"),
    (Join-Path $root "run_single_client.ps1"),
    (Join-Path $root "run_network_server.ps1"),
    (Join-Path $root "README.md")
) -DestinationPath $lanPackage -Force

# Portable one-click package for a participant testing the complete two-pet
# flow on one Windows PC.  PlanePetFullFlow already contains its server and
# both clients, so the tester only needs the renamed executable.
$testerFolder = Join-Path $dist "PlanePet-Tester"
New-Item -ItemType Directory -Force -Path $testerFolder | Out-Null
Copy-Item -LiteralPath (Join-Path $dist "PlanePetFullFlow.exe") `
    -Destination (Join-Path $testerFolder "PlanePet.exe") -Force
$testerInstructions = @(
    'Plane Pet 测试版（Windows 10/11 64 位）',
    '',
    '1. 双击 PlanePet.exe，无需安装，也无需联网。',
    '2. 程序会自动打开两个桌宠。首次启动时两个客户端会分别询问是否允许在本机记录匿名实验事件；选择“是”或“否”都可继续使用。',
    '3. 在两个桌宠中输入任意相同的六位匹配码，然后分别按 Enter。',
    '4. 匹配成功后，鼠标移到桌宠上点“开一局”，或右键选“邀请对战”；在另一端点“接受”。◀ 展开表情，▶ 收起；移出后隐藏但保留展开，10 分钟未发送表情才收起。闲置时自动射击，方向随机偏转左右各 15°，左键不控制射击。',
    '5. 游戏中使用 WASD、方向键，或按住鼠标左键拖动飞机。',
    '6. 结束测试时，请分别右键两架飞机并选择“退出”；内置本地服务会自动关闭。',
    '',
    '隐私说明：实验事件和战绩只保存在本机；事件范围与程序授权弹窗一致。不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题，也不会自动上传；在右键“设置与隐私”中导出统计记录、清除本地战绩与统计或关闭匿名使用统计。操作帮助在“帮助与更新”。',
    '若 Windows SmartScreen 提示未知发布者，是因为当前测试构建尚未配置商业代码签名证书；确认文件来自测试发起者后，可选择“更多信息”→“仍要运行”。'
)
Set-Content -LiteralPath (Join-Path $testerFolder "使用说明.txt") `
    -Value $testerInstructions -Encoding utf8
$testerPackage = Join-Path $dist "PlanePet-Tester.zip"
Compress-Archive -Path (Join-Path $testerFolder "*") `
    -DestinationPath $testerPackage -Force
$hashTargets = @(
    (Join-Path $dist "PlanePetFullFlow.exe"),
    (Join-Path $dist "PlanePetClient.exe"),
    (Join-Path $dist "PlanePetServer.exe"),
    $lanPackage,
    $testerPackage
)
if ($null -ne $publicPackage) {
    $hashTargets += (Join-Path $dist "PlanePetPublic.exe")
    $hashTargets += $publicPackage
}
if ($null -ne $publicDualPackage) {
    $hashTargets += (Join-Path $dist "PlanePetPublicDual.exe")
    $hashTargets += $publicDualPackage
}
$hashLines = foreach ($target in $hashTargets) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $target
    "$($hash.Hash.ToLower())  $([IO.Path]::GetFileName($target))"
}
Set-Content -LiteralPath (Join-Path $dist "SHA256SUMS.txt") `
    -Value $hashLines -Encoding ascii

Write-Host "PC_PET_BUILD_OK: $dist"
