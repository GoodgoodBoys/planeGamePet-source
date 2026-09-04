param(
    [string]$Compiler = "C:\Installation\Lib\mingw64\bin\g++.exe",
    [string]$ResourceCompiler = "C:\Installation\Lib\mingw64\bin\windres.exe",
    [string]$PublicUrl = "wss://8.166.124.212:32112/v1/tunnel",
    [switch]$BuildPublic
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$appVersion = "1.0.0"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "C++ compiler not found: $Compiler"
}
$compilerBin = Split-Path -Parent $Compiler

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
}

if (-not (Test-Path -LiteralPath $ResourceCompiler)) {
    throw "Resource compiler not found: $ResourceCompiler"
}
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
    if ($PublicUrl -notmatch '^wss://') {
        throw "PublicUrl must use wss://."
    }
    if ($PublicUrl -ne 'wss://8.166.124.212:32112/v1/tunnel') {
        throw "This pilot build currently supports the configured ECS WSS URL only."
    }
    Build-One @(
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
        "-mwindows"
    )
    $publicFolder = Join-Path $dist "PlanePet-Public-Single-$appVersion"
    New-Item -ItemType Directory -Force -Path $publicFolder | Out-Null
    Copy-Item -LiteralPath (Join-Path $dist "PlanePetPublic.exe") `
        -Destination (Join-Path $publicFolder "PlanePet.exe") -Force
    $publicInstructions = @(
        "Plane Pet 单端公网最终测试版 v$appVersion（Windows 10/11 64 位）",
        '',
        '1. 将这个压缩包分别发给两位测试者；每台电脑各解压并运行一份。',
        '2. 双击 PlanePet.exe，无需安装、无需配置服务器，也无需允许防火墙 UDP 入站。',
        '3. 首次运行会询问是否允许发送匿名测试统计；选择“是”或“否”均可继续使用。',
        '4. 两位测试者输入任意相同的六位匹配码并按 Enter。输入框和匹配信息卡可按住左键拖动；右键取消勾选“显示绑定电脑框”可只看飞行动画，搜索中可按 Esc 或右键停止。',
        '5. 匹配成功会永久保存在本机；以后启动自动恢复，无需再次输入匹配码。',
        '6. 右键飞机可邀请对战、查看战绩/帮助、暂停邀请、隐藏、解除绑定或退出。',
        '7. 双方在线时会显示 D2 像素红蓝飞机转弯追逐动画；点击底部四个表情可发送给好友。',
        '8. 邀请最长等待 5 分钟；对方接受后打开小型游戏窗口，最长对局 3 分钟。',
        '9. 游戏使用 WASD、方向键，或按住鼠标左键拖动飞机；结束后单击或按键返回桌宠。',
        '10. 隐藏桌宠后收到邀请会自动弹出。要彻底关闭程序，请右键飞机或托盘图标选择“退出”。',
        '11. 程序会安全检查新版本；非必要更新可选择 7 天后提醒。也可在右键菜单选择“检查软件更新…”。大版本不兼容时，联机功能会暂停到旧版一方完成升级。',
        '',
        '联网说明：程序通过 TLS 加密连接测试服务器；匹配码只用于当次寻找正在等待的另一端。',
        '绑定存储：匹配成功后，本机和服务器保存随机设备标识及绑定令牌，用于下次自动恢复；不保存姓名，匹配码不会作为绑定凭据保存。',
        '联网鉴权：无论是否同意匿名统计，服务器都需保存随机安装凭据的不可逆哈希、登记时间和防滥用限流所需的来源网络信息；原始安装凭据不会上传。这类联网鉴权数据不属于可选匿名统计。',
        '隐私说明：同意后发送启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天，可在右键菜单随时停止。',
        '使用限制：一台电脑只运行一个单端实例；请勿同时运行单机双端测试版，否则本地端口会冲突。',
        '若 Windows SmartScreen 提示未知发布者，是因为测试构建尚未配置商业代码签名证书。'
    )
    Set-Content -LiteralPath (Join-Path $publicFolder "使用说明.txt") `
        -Value $publicInstructions -Encoding utf8
    $publicPackage = Join-Path $dist "PlanePet-Public-Single-$appVersion.zip"
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
    $publicDualFolder = Join-Path $dist "PlanePet-Public-DualLocal-$appVersion"
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
        '5. 绿色圆点表示另一端在线。右键任一飞机选择“邀请对战”，在另一端点击“接受”。',
        '6. 双方在线时会显示 D2 像素红蓝飞机转弯追逐动画；点击底部四个表情可测试双向发送。',
        '7. 游戏使用 WASD、方向键，或按住鼠标左键拖动飞机。',
        '8. 结束时请分别右键两个桌宠并选择“退出”；启动器随后自动关闭两条公网隧道。',
        '',
        '重启验证：匹配成功后退出两个桌宠，再次双击 PlanePet.exe，应自动恢复永久绑定。',
        '说明：两端拥有独立设备身份、DPAPI 凭据、存档和本地 UDP 端口，但共享这台电脑的公网出口。',
        '绑定存储：匹配成功后，本机和服务器保存随机设备标识及绑定令牌，用于下次自动恢复；不保存姓名，匹配码不会作为绑定凭据保存。',
        '联网鉴权：无论是否同意匿名统计，服务器都需保存随机安装凭据的不可逆哈希、登记时间和防滥用限流所需的来源网络信息；原始安装凭据不会上传。这类联网鉴权数据不属于可选匿名统计。',
        '隐私说明：同意后发送启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天，可在右键菜单随时停止。',
        '不要同时运行普通公网版或其他单机双端测试实例，否则本地端口可能冲突。',
        '若 Windows SmartScreen 提示未知发布者，是因为测试构建尚未配置商业代码签名证书。'
    )
    Set-Content -LiteralPath (Join-Path $publicDualFolder "使用说明.txt") `
        -Value $publicDualInstructions -Encoding utf8
    $publicDualPackage = Join-Path $dist "PlanePet-Public-DualLocal-$appVersion.zip"
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
    '4. 匹配成功后，右键任意飞机，选择“邀请对战”；在另一架飞机上选择“接受”。',
    '5. 游戏中使用 WASD、方向键，或按住鼠标左键拖动飞机。',
    '6. 结束测试时，请分别右键两架飞机并选择“退出”；内置本地服务会自动关闭。',
    '',
    '隐私说明：实验事件和战绩只保存在本机；事件范围与程序授权弹窗一致。不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题，也不会自动上传；可在右键菜单中导出或清除。',
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
