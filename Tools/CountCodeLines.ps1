<#
.SYNOPSIS
    统计 Orbeden 仓库中非第三方代码的行数（C++ / C#）。

.DESCRIPTION
    扫描 OrbedenCore、OrbedenEditor、OrbedenGame、Tools 四个代码根，跳过第三方库、
    已发布的 SDK 副本、生成物与构建目录。逐行分类为代码 / 注释 / 空行：
    块注释、字符串字面量、C# 逐字字符串（@"..."）与原始字符串（"""..."""）按状态机处理，
    注释里的引号、字符串里的 // 都不会误判。

.PARAMETER Root
    相对仓库根目录的扫描起点，默认四个代码根。

.PARAMETER IncludeGenerated
    计入 Generated/ 目录（MetaGen 生成物）。默认排除，但仍单独报告其规模。

.PARAMETER ByFile
    输出逐文件明细，按代码行数降序。

.PARAMETER PassThru
    除了屏幕输出，再把按模块汇总的对象写入管道，便于二次处理。

.PARAMETER Csv
    把逐文件明细导出为 CSV。

.EXAMPLE
    ./Tools/CountCodeLines.ps1

.EXAMPLE
    ./Tools/CountCodeLines.ps1 -ByFile -Csv ./.loc.csv
#>
[CmdletBinding()]
param(
    [string[]]$Root = @('OrbedenCore', 'OrbedenEditor', 'OrbedenGame', 'Tools'),
    [switch]$IncludeGenerated,
    [switch]$ByFile,
    [switch]$PassThru,
    [string]$Csv
)

$ErrorActionPreference = 'Stop'

# 统计结果含中文，统一用 UTF-8 输出，避免控制台代码页把表头显示成乱码。
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $repoRoot) { $repoRoot = (Get-Location).Path }

# 目录名一旦匹配就整棵跳过，大小写不敏感。
$excludedDirectories = @(
    'ThirdParty', 'Sdk', 'x64', 'obj', 'bin', '.vs', '.git',
    'Build', 'Log', 'Tests', 'Legacy', 'Others', '.cache', 'node_modules'
)

$languageByExtension = @{
    '.c'   = 'C++'
    '.cc'  = 'C++'
    '.cpp' = 'C++'
    '.cxx' = 'C++'
    '.h'   = 'C++'
    '.hpp' = 'C++'
    '.inl' = 'C++'
    '.cs'  = 'C#'
}

# 命中前缀越长的规则优先，用于给统计结果分组。
$moduleRules = @(
    [pscustomobject]@{ Prefix = 'OrbedenCore/Src';         Name = 'OrbedenCore 原生' }
    [pscustomobject]@{ Prefix = 'OrbedenCore/Managed';     Name = 'OrbedenCore 托管' }
    [pscustomobject]@{ Prefix = 'OrbedenEditor/Src';       Name = 'OrbedenEditor 原生' }
    [pscustomobject]@{ Prefix = 'OrbedenEditor/Managed';   Name = 'OrbedenEditor 托管' }
    [pscustomobject]@{ Prefix = 'OrbedenEditor/Templates'; Name = '模板工程' }
    [pscustomobject]@{ Prefix = 'OrbedenGame';             Name = 'OrbedenGame' }
    [pscustomobject]@{ Prefix = 'Tools';                   Name = 'Tools' }
) | Sort-Object { $_.Prefix.Length } -Descending

$charNewline = [char]10
$charSlash = [char]47
$charStar = [char]42
$charQuote = [char]34
$charApostrophe = [char]39
$charBackslash = [char]92
$charAt = [char]64

function Get-ModuleName {
    param([string]$RelativePath)

    foreach ($rule in $moduleRules) {
        if ($RelativePath.StartsWith($rule.Prefix + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
            return $rule.Name
        }
    }
    return $RelativePath.Split('/')[0]
}

function Measure-SourceFile {
    param(
        [string]$Path,
        [bool]$IsCSharp
    )

    $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
    if ([string]::IsNullOrEmpty($text)) {
        return [pscustomobject]@{ Total = 0; Code = 0; Comment = 0; Blank = 0 }
    }

    $lineCount = $text.Split([char[]]@($charNewline)).Length
    if ($text[$text.Length - 1] -eq $charNewline) { $lineCount-- }
    if ($lineCount -lt 1) { $lineCount = 1 }

    # 多分配一行，扫描时不必做边界判断。
    $hasCode = New-Object 'bool[]' ($lineCount + 1)
    $hasNote = New-Object 'bool[]' ($lineCount + 1)

    $length = $text.Length
    $state = 'Code'
    $line = 0
    $rawQuotes = 0
    $i = 0

    while ($i -lt $length) {
        $c = $text[$i]

        if ($c -eq $charNewline) {
            # 行注释在换行处结束；字符串可以跨行（VerbatimString / RawString）。
            if ($state -eq 'LineComment') { $state = 'Code' }
            $line++
            $i++
            continue
        }

        $next = if ($i + 1 -lt $length) { $text[$i + 1] } else { [char]0 }

        if ($state -eq 'Code') {
            # 注释起始符记为注释而非代码，否则纯注释行会因为开头的 // 或 /* 被记成代码行；
            # 只含起始符的行（如独占一行的 /*）也要靠这一笔记成注释行而不是空行。
            if ($c -eq $charSlash -and $next -eq $charSlash) { $hasNote[$line] = $true; $state = 'LineComment'; $i += 2; continue }
            if ($c -eq $charSlash -and $next -eq $charStar)  { $hasNote[$line] = $true; $state = 'BlockComment'; $i += 2; continue }

            if (-not [char]::IsWhiteSpace($c)) { $hasCode[$line] = $true }

            if ($c -eq $charQuote) {
                $q = 1
                while ($i + $q -lt $length -and $text[$i + $q] -eq $charQuote) { $q++ }
                if ($IsCSharp -and $q -ge 3) { $state = 'RawString'; $rawQuotes = $q; $i += $q; continue }
                $state = 'String'
                $i++
                continue
            }

            if ($IsCSharp -and $c -eq $charAt -and $next -eq $charQuote) { $state = 'VerbatimString'; $i += 2; continue }

            if ($c -eq $charApostrophe) {
                # C++14 数字分隔符（1'000）不是字符字面量：前面紧跟字母或数字时不当引号处理。
                $prev = if ($i -gt 0) { $text[$i - 1] } else { [char]0 }
                if (-not [char]::IsLetterOrDigit($prev)) { $state = 'Char'; $i++; continue }
            }

            $i++
            continue
        }

        if ($state -eq 'String') {
            $hasCode[$line] = $true
            if ($c -eq $charBackslash -and $next -ne $charNewline) { $i += 2; continue }
            if ($c -eq $charQuote) { $state = 'Code'; $i++; continue }
            $i++
            continue
        }

        if ($state -eq 'VerbatimString') {
            $hasCode[$line] = $true
            if ($c -eq $charQuote -and $next -eq $charQuote) { $i += 2; continue }
            if ($c -eq $charQuote) { $state = 'Code'; $i++; continue }
            $i++
            continue
        }

        if ($state -eq 'RawString') {
            $hasCode[$line] = $true
            if ($c -eq $charQuote) {
                $q = 1
                while ($i + $q -lt $length -and $text[$i + $q] -eq $charQuote) { $q++ }
                if ($q -ge $rawQuotes) { $state = 'Code' }
                $i += $q
                continue
            }
            $i++
            continue
        }

        if ($state -eq 'Char') {
            $hasCode[$line] = $true
            if ($c -eq $charBackslash -and $next -ne $charNewline) { $i += 2; continue }
            if ($c -eq $charApostrophe) { $state = 'Code'; $i++; continue }
            $i++
            continue
        }

        if ($state -eq 'LineComment') {
            if (-not [char]::IsWhiteSpace($c)) { $hasNote[$line] = $true }
            $i++
            continue
        }

        if ($state -eq 'BlockComment') {
            if (-not [char]::IsWhiteSpace($c)) { $hasNote[$line] = $true }
            if ($c -eq $charStar -and $next -eq $charSlash) { $state = 'Code'; $i += 2; continue }
            $i++
            continue
        }

        $i++
    }

    $code = 0
    $comment = 0
    $blank = 0
    for ($l = 0; $l -lt $lineCount; $l++) {
        if ($hasCode[$l]) { $code++ }
        elseif ($hasNote[$l]) { $comment++ }
        else { $blank++ }
    }

    return [pscustomobject]@{ Total = $lineCount; Code = $code; Comment = $comment; Blank = $blank }
}

function Get-SourceFileList {
    param(
        [string]$Directory,
        [string]$RelativeBase
    )

    foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($Directory)) {
        $name = [System.IO.Path]::GetFileName($entry)
        $relative = if ($RelativeBase) { "$RelativeBase/$name" } else { $name }

        if ([System.IO.Directory]::Exists($entry)) {
            if ($excludedDirectories -contains $name) { continue }
            Get-SourceFileList -Directory $entry -RelativeBase $relative
            continue
        }

        $extension = [System.IO.Path]::GetExtension($entry).ToLowerInvariant()
        if (-not $languageByExtension.ContainsKey($extension)) { continue }

        # 生成物单独记账，默认不计入统计。
        $isGenerated = $relative.Contains('/Generated/')
        if ($isGenerated -and -not $IncludeGenerated) {
            $script:generatedFiles += $relative
            continue
        }

        [pscustomobject]@{ AbsolutePath = $entry; RelativePath = $relative }
    }
}

$script:generatedFiles = @()
$records = New-Object System.Collections.Generic.List[object]

foreach ($rootName in $Root) {
    $rootPath = Join-Path $repoRoot $rootName
    if (-not [System.IO.Directory]::Exists($rootPath)) {
        Write-Warning "跳过不存在的代码根目录: $rootName"
        continue
    }

    foreach ($file in Get-SourceFileList -Directory $rootPath -RelativeBase $rootName) {
        $language = $languageByExtension[[System.IO.Path]::GetExtension($file.AbsolutePath).ToLowerInvariant()]
        $measure = Measure-SourceFile -Path $file.AbsolutePath -IsCSharp ($language -eq 'C#')

        $records.Add([pscustomobject]@{
            Module       = Get-ModuleName $file.RelativePath
            Language     = $language
            文件          = $file.RelativePath
            Total        = $measure.Total
            Code         = $measure.Code
            Comment      = $measure.Comment
            Blank        = $measure.Blank
        })
    }
}

if ($records.Count -eq 0) {
    Write-Warning '没有扫到任何源文件，请检查 -Root 参数。'
    return
}

$summary = $records | Group-Object { $_.Module + '|' + $_.Language } | ForEach-Object {
    [pscustomobject]@{
        模块  = $_.Group[0].Module
        语言  = $_.Group[0].Language
        文件  = $_.Count
        总行  = [int]($_.Group | Measure-Object Total   -Sum).Sum
        代码  = [int]($_.Group | Measure-Object Code    -Sum).Sum
        注释  = [int]($_.Group | Measure-Object Comment -Sum).Sum
        空行  = [int]($_.Group | Measure-Object Blank   -Sum).Sum
    }
} | Sort-Object 代码 -Descending

$languageSummary = $records | Group-Object Language | ForEach-Object {
    [pscustomobject]@{
        语言 = $_.Name
        文件 = $_.Count
        总行 = [int]($_.Group | Measure-Object Total   -Sum).Sum
        代码 = [int]($_.Group | Measure-Object Code    -Sum).Sum
        注释 = [int]($_.Group | Measure-Object Comment -Sum).Sum
        空行 = [int]($_.Group | Measure-Object Blank   -Sum).Sum
    }
} | Sort-Object 代码 -Descending

$totalFiles   = ($summary | Measure-Object 文件 -Sum).Sum
$totalLines   = ($summary | Measure-Object 总行 -Sum).Sum
$totalCode    = ($summary | Measure-Object 代码 -Sum).Sum
$totalComment = ($summary | Measure-Object 注释 -Sum).Sum
$totalBlank   = ($summary | Measure-Object 空行 -Sum).Sum

Write-Host ''
Write-Host 'Orbeden 代码行统计（非第三方）' -ForegroundColor Cyan
Write-Host ("扫描根  : " + ($Root -join ', '))
Write-Host ("排除目录: " + ($excludedDirectories -join ', '))
if (-not $IncludeGenerated) { Write-Host '          Generated（生成物，默认排除）' }

Write-Host ''
Write-Host '按模块'
$summary | Format-Table -AutoSize | Out-Host

Write-Host '按语言'
$languageSummary | Format-Table -AutoSize | Out-Host

Write-Host ("合计: {0} 个文件, {1} 行（代码 {2} / 注释 {3} / 空行 {4}）" -f `
    $totalFiles, $totalLines, $totalCode, $totalComment, $totalBlank) -ForegroundColor Green

foreach ($language in $languageSummary) {
    Write-Host ("  {0,-4} 代码 {1,7} 行, 文件 {2,4} 个" -f $language.语言, $language.代码, $language.文件)
}

if ($script:generatedFiles.Count -gt 0) {
    $generatedLines = 0
    foreach ($relative in $script:generatedFiles) {
        $absolute = Join-Path $repoRoot $relative
        $generatedLines += ([System.IO.File]::ReadAllLines($absolute, [System.Text.Encoding]::UTF8)).Length
    }
    Write-Host ("未计入的生成文件: {0} 个, {1} 行（-IncludeGenerated 可计入）" -f `
        $script:generatedFiles.Count, $generatedLines) -ForegroundColor DarkGray
}

if ($ByFile) {
    Write-Host ''
    Write-Host '逐文件明细（按代码行降序）'
    $records | Sort-Object Code -Descending |
        Select-Object @{ Name = '文件'; Expression = { $_.文件 } }, 语言, 总行, 代码, 注释, 空行 |
        Format-Table -AutoSize | Out-Host
}

if ($Csv) {
    $csvPath = if ([System.IO.Path]::IsPathRooted($Csv)) { $Csv } else { Join-Path $repoRoot $Csv }
    $records | Select-Object Module, Language, @{ Name = 'File'; Expression = { $_.文件 } }, Total, Code, Comment, Blank |
        Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
    Write-Host "已导出逐文件明细: $csvPath" -ForegroundColor DarkGray
}

if ($PassThru) {
    $summary
}
