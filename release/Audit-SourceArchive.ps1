param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$paths = @(& git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard | Sort-Object -Unique)
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate this source repository' }
$allowedRoots = @('assets','common','deploy','desktop','gateway','launcher','public_launcher',
    'public_dual_launcher','release','server','shared','tests','tunnel','updater')
$textExtensions = @('.cpp','.h','.ps1','.py','.md','.txt','.rc','.manifest','.json',
    '.service','.timer','.ini','.sh','.conf','.example','.html','.css','.js','.cmd','.bat','.cs','.svg')
$imageExtensions = @('.png','.gif','.ico','.bmp','.apng')
$records = @()
$issues = @()
foreach ($relative in $paths) {
    $file = [IO.Path]::GetFullPath((Join-Path $repo $relative))
    if (-not $file.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Archive path escaped the project'
    }
    $info = Get-Item -LiteralPath $file -Force
    if ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Symlink not allowed: $relative" }
    $top = ($relative -split '/')[0]
    $extension = $info.Extension.ToLowerInvariant()
    if (($relative.Contains('/') -and $top -notin $allowedRoots) -or
        ($extension -notin ($textExtensions + $imageExtensions) -and
         $relative -notin @('.gitignore','.gitattributes','LICENSE','COPYING'))) {
        $issues += "Unreviewed path/type: $relative"; continue
    }
    if ($relative -match '(?i)(^|/)(dist|release_out|\.git|\.env)(/|$)|\.(pem|key|pfx|p12|csp|credential|binding|settings|history|db|sqlite|log|csv)$') {
        $issues += "Sensitive/runtime path: $relative"; continue
    }
    if ($info.Length -gt 10MB) { $issues += "Unexpected large source: $relative"; continue }
    if ($extension -in $imageExtensions) {
        $bytes = [IO.File]::ReadAllBytes($file)
        $magic = [Convert]::ToHexString($bytes[0..([Math]::Min(7,$bytes.Length-1))])
        if (($extension -in @('.png','.apng') -and $magic -ne '89504E470D0A1A0A') -or
            ($extension -eq '.gif' -and -not $magic.StartsWith('47494638')) -or
            ($extension -eq '.ico' -and -not $magic.StartsWith('00000100')) -or
            ($extension -eq '.bmp' -and -not $magic.StartsWith('424D'))) {
            $issues += "Invalid asset format: $relative"; continue
        }
    } else {
        $body = [IO.File]::ReadAllText($file)
        # Report filenames only; never echo a possible matched secret.
        if ($body -match '-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----|LTAI[0-9A-Za-z]{16,}|gh[pousr]_[0-9A-Za-z]{30,}|github_pat_[0-9A-Za-z_]{30,}') {
            $issues += "Possible private credential: $relative"; continue
        }
    }
    $records += [pscustomobject]@{ path=$relative; bytes=$info.Length;
        sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower() }
}
if ($issues.Count) { $issues; throw 'Source archive requires additional review; nothing staged' }
$audit = Join-Path $repo 'dist/source-archive-audit.json'
[IO.File]::WriteAllText($audit, ($records | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
$records | Group-Object { ($_.path -split '/')[0] } | Select-Object Name,Count
[pscustomobject]@{ Result='SOURCE_ARCHIVE_AUDIT_OK'; Files=$records.Count;
    Bytes=($records | Measure-Object bytes -Sum).Sum; Manifest=$audit;
    ManifestSha256=(Get-FileHash -LiteralPath $audit).Hash.ToLower() }
