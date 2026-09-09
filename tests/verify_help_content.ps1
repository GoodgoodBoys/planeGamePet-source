$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskHeader = Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'desktop\help_content.h')
$taskDocument = Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'HELP.md')

function Normalize-HelpText([string]$Value) {
    # Ignore presentation only: Markdown headings/lists, the inline icon legend
    # (tested separately), and whitespace. Preserve all words and punctuation.
    $Value = $Value -replace '(?m)^>[^\r\n]*', ''
    $Value = $Value -replace '(?m)^〔[^\r\n]*', ''
    $Value = $Value -replace '(?m)^#{1,6}\s+', ''
    $Value = $Value -replace '(?m)^[•-]\s+', ''
    return $Value -replace '\s+', ''
}

$taskSectionPattern = '(?s)\{L"(?<heading>[^"\r\n]+)",\s*(?<body>(?:L"(?:[^"\\]|\\.)*"\s*)+),\s*\d+\}'
$taskCount = 0
foreach ($taskPage in @(@('kBriefSections', '简略帮助', 5), @('kSections', '详细帮助', 13))) {
    $taskArray = [regex]::Match($taskHeader, '(?s)Section ' + $taskPage[0] + '\[\] = \{(?<items>.*?)\r?\n\};')
    $taskSections = [regex]::Matches($taskArray.Groups['items'].Value, $taskSectionPattern)
    if ($taskSections.Count -ne $taskPage[2]) { throw "Unexpected section count: $($taskPage[1])" }
    $taskPageText = [regex]::Match($taskDocument, '(?ms)^## ' + $taskPage[1] + '\r?\n(?<body>.*?)(?=^## |\z)').Groups['body'].Value
    foreach ($taskSection in $taskSections) {
        $taskHeading = $taskSection.Groups['heading'].Value
        $taskFragments = [regex]::Matches($taskSection.Groups['body'].Value, 'L"(?<text>(?:[^"\\]|\\.)*)"')
        $taskExpected = ($taskFragments | ForEach-Object { [regex]::Unescape($_.Groups['text'].Value) }) -join ''
        $taskActual = [regex]::Match($taskPageText, '(?ms)^### ' + [regex]::Escape($taskHeading) + '\r?\n(?<body>.*?)(?=^### |\z)').Groups['body'].Value
        $taskActual = $taskActual -replace '(?m)^提醒：[^\r\n]*', '' -replace '(?m)^← 返回简略帮助[^\r\n]*', ''
        if ((Normalize-HelpText $taskExpected) -cne (Normalize-HelpText $taskActual)) {
            throw "Help content mismatch: $($taskPage[1]) / $taskHeading"
        }
        $taskCount++
    }
}
foreach ($taskLabel in @('朋友在线', '朋友离线', '勿扰')) {
    if (-not $taskDocument.Contains($taskLabel) -or -not $taskHeader.Contains('L"' + $taskLabel + '"')) {
        throw "Missing status label: $taskLabel"
    }
}
foreach ($taskConstant in @('kSubtitle', 'kBriefSubtitle', 'kBriefNotice', 'kDetailLink', 'kBackLink')) {
    $taskText = [regex]::Match($taskHeader, '(?s)' + $taskConstant + '\[\]\s*=\s*L"(?<text>[^"\r\n]+)"').Groups['text'].Value
    if (-not $taskText -or -not $taskDocument.Contains($taskText)) { throw "Missing shared help text: $taskConstant" }
}
"HELP_CONTENT_SYNC_OK sections=$taskCount brief=5 detailed=13"
