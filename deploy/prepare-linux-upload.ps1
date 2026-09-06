$ErrorActionPreference = 'Stop'
$planeRoot = Split-Path -Parent $PSScriptRoot
$planeArchive = Join-Path $planeRoot 'dist\plane-pet-linux-source-1.0.1-20260905.tgz'
$planeExpected = 'FC1C9372BB136DA45FFBE194D94B60CD9C7EBB8ECBD78E7EADB54F9E5813D044'
if ((Get-FileHash -LiteralPath $planeArchive -Algorithm SHA256).Hash -ne $planeExpected) {
    throw 'Source archive changed; revalidate before upload.'
}
$planeParts = Join-Path $planeRoot 'dist\linux-upload-1.0.1-20260905'
if (Test-Path -LiteralPath $planeParts) { throw 'Upload part directory already exists.' }
New-Item -ItemType Directory -Path $planeParts | Out-Null
$planeBytes = [IO.File]::ReadAllBytes($planeArchive)
for ($planeOffset = 0; $planeOffset -lt $planeBytes.Length; $planeOffset += 22000) {
    $planeCount = [Math]::Min(22000, $planeBytes.Length - $planeOffset)
    $planeSlice = [byte[]]::new($planeCount)
    [Array]::Copy($planeBytes, $planeOffset, $planeSlice, 0, $planeCount)
    $planeName = 'source.part{0:D2}' -f [int]($planeOffset / 22000)
    $planeFile = Join-Path $planeParts $planeName
    [IO.File]::WriteAllBytes($planeFile, $planeSlice)
    Get-FileHash -LiteralPath $planeFile -Algorithm SHA256
}
Write-Output 'PLANE_PET_UPLOAD_PARTS_READY'
