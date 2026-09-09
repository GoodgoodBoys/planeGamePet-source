$ErrorActionPreference = 'Stop'

function ConvertTo-PlanePetVersion([string]$Text) {
    if ($Text -notmatch '^(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})$') { throw "Noncanonical release version: $Text" }
    if (@($Text.Split('.') | Where-Object { [int]$_ -gt 255 }).Count) { throw 'Version components must be 0..255' }
    return [version]$Text
}

function Test-PlanePetReleaseArtifact([string]$Executable, [string]$Version,
                                    [string]$MinimumVersion, [switch]$AllowUnsigned) {
    $target = ConvertTo-PlanePetVersion $Version
    $minimum = ConvertTo-PlanePetVersion $MinimumVersion
    if ($target -lt [version]'1.0.0' -or $minimum -lt [version]'1.0.0' -or $minimum -gt $target) { throw 'Invalid minimum/public version relationship' }
    $file = Get-Item -LiteralPath $Executable -ErrorAction Stop
    if ($file.PSIsContainer -or $file.Length -lt 1MB -or $file.Length -gt 64MB) { throw 'Invalid launcher size/type' }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName)
    if ($info.FileVersion -ne $Version -or $info.ProductVersion -ne $Version -or
        "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart)" -ne $Version -or
        $info.FilePrivatePart -ne 0 -or $info.ProductName -ne 'Plane Pet') { throw "Embedded EXE version/product does not match $Version" }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -eq 'NotSigned') {
        if (-not $AllowUnsigned) { throw 'Unsigned release requires explicit -AllowUnsigned approval' }
    } elseif ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate) { throw 'Invalid or untimestamped Authenticode signature' }
    if (-not ('PlanePetReleaseGate' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PlanePetReleaseGate {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryExW(string p, IntPtr f, uint flags);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindResourceW(IntPtr m, IntPtr n, IntPtr t);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr m, IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr m, IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr r);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr m);
}
'@
    }
    $module = [PlanePetReleaseGate]::LoadLibraryExW($file.FullName, [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot inspect launcher resources' }
    try {
        foreach ($id in @(201,202,203,204,205,206)) {
            $resource = [PlanePetReleaseGate]::FindResourceW($module, [IntPtr]$id, [IntPtr]10)
            $size = [PlanePetReleaseGate]::SizeofResource($module, $resource)
            if (-not $size) { throw "Incomplete single-client release: missing resource $id" }
            if ($id -eq 206) {
                $pointer = [PlanePetReleaseGate]::LockResource([PlanePetReleaseGate]::LoadResource($module, $resource))
                $bytes = [byte[]]::new($size)
                [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $size)
                if ([Text.Encoding]::UTF8.GetString($bytes).Trim() -ne 'plane-pet-windows/release/1') { throw 'Wrong release epoch/channel' }
            }
        }
    } finally { [void][PlanePetReleaseGate]::FreeLibrary($module) }
    return [pscustomobject]@{ Path=$file.FullName; Size=$file.Length;
        Sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLower();
        Authenticode=$signature.Status.ToString() }
}

function Assert-PlanePetImmutableArtifact([string]$Path, [string]$Sha256) {
    if ((Test-Path -LiteralPath $Path) -and (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash -ne $Sha256) {
        throw "Refusing different artifact for the same version: $Path"
    }
}
