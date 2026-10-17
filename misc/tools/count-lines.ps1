<#
.SYNOPSIS
    Counts lines of source code in a project, ignoring dependencies and build outputs.

.DESCRIPTION
    Recursively counts lines for common source/config file extensions while skipping
    directories such as .git, .vs, x64, node_modules, bin, obj, Debug and Release.

.EXAMPLE
    .\Count-Lines.ps1

.EXAMPLE
    .\Count-Lines.ps1 -Path C:\Projects\MyApp -IncludeDocs

.EXAMPLE
    .\Count-Lines.ps1 -IncludeExtension .cpp,.h,.hpp
#>

[CmdletBinding()]
param(
    [string]$Path,

    [string[]]$IncludeExtension = @(
        '.c', '.cc', '.cpp', '.cxx',
        '.h', '.hh', '.hpp', '.hxx',
        '.cs', '.js', '.mjs', '.ts', '.tsx',
        '.py', '.java', '.go', '.rs', '.php', '.rb',
        '.sh', '.ps1',
        '.qml', '.ui', '.qrc', '.rc',
        '.json', '.xml', '.yaml', '.yml', '.toml', '.ini',
        '.cmake', '.sln', '.slnx', '.vcxproj', '.filters'
    ),

    [string[]]$ExcludeDirectory = @(
        '.git', '.vs', 'x64', 'node_modules',
        'bin', 'obj', 'Debug', 'Release',
        'out', 'build', 'dist'
    ),

    [switch]$IncludeDocs,

    # By default the line-counting script itself is not counted.
    [switch]$IncludeSelf
)

$ErrorActionPreference = 'Stop'

# Default to the directory containing this script, so it can be run from anywhere.
if (-not $Path) {
    # This script lives in misc/tools, so default to the repository root (two levels up).
    $Path = if ($PSScriptRoot) { (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path } else { (Get-Location).Path }
    # 那时它必须放在仓库根才能用 —— 这就是"路径问题"。
}

# Optional documentation / markup files.
if ($IncludeDocs) {
    $IncludeExtension += @('.md', '.txt', '.rst', '.gitignore', '.editorconfig')
}

# Normalize extensions to lower-case and ensure they start with a dot.
# Also split comma-separated values so the script works from powershell -File too.
$IncludeExtension = $IncludeExtension | ForEach-Object {
    $_ -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
} | ForEach-Object {
    $ext = $_.ToLowerInvariant()
    if (-not $ext.StartsWith('.')) { $ext = ".$ext" }
    $ext
} | Sort-Object -Unique

$ExcludeDirectory = $ExcludeDirectory | ForEach-Object {
    $_ -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
} | Sort-Object -Unique

function Get-FileLineCount {
    param([System.IO.FileInfo]$File)

    $count = 0
    $reader = [System.IO.StreamReader]::new($File.FullName)
    try {
        while ($null -ne $reader.ReadLine()) {
            $count++
        }
    }
    finally {
        $reader.Dispose()
    }

    return $count
}

# Resolve the target path so relative paths work.
$target = Get-Item -LiteralPath $Path
if (-not $target) {
    throw "Path not found: $Path"
}

$rootFull = $target.FullName.TrimEnd('\', '/')

Write-Host "Counting lines under: $rootFull" -ForegroundColor Cyan

$files = Get-ChildItem -LiteralPath $target.FullName -Recurse -File -Force -ErrorAction SilentlyContinue | Where-Object {
    $file = $_
    $lowerExt = $file.Extension.ToLowerInvariant()

    # Skip the running script itself unless -IncludeSelf is used.
    if (-not $IncludeSelf -and $PSCommandPath -and $file.FullName -eq $PSCommandPath) {
        return $false
    }

    # Skip files inside excluded directories.
    $inExcludedDir = $ExcludeDirectory | Where-Object {
        $dir = $_
        $file.FullName -match "(?:^|\\)$([regex]::Escape($dir))(?:\\|$)"
    }
    if ($inExcludedDir) { return $false }

    # Keep only the requested source/config extensions.
    if ($IncludeExtension -notcontains $lowerExt) { return $false }

    return $true
}

if (-not $files) {
    Write-Host "No matching files found." -ForegroundColor Yellow
    return
}

# Compute line counts.
$fileStats = foreach ($file in $files) {
    [PSCustomObject]@{
        Path       = $file.FullName
        Relative   = if ($file.FullName.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                         $file.FullName.Substring($rootFull.Length).TrimStart('\', '/')
                     } else {
                         $file.FullName
                     }
        Extension  = $file.Extension.ToLowerInvariant()
        Directory  = if ($file.DirectoryName.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                         $file.DirectoryName.Substring($rootFull.Length).TrimStart('\', '/')
                     } else {
                         $file.DirectoryName
                     }
        Lines      = Get-FileLineCount -File $file
    }
}

$totalFiles = $fileStats.Count
$totalLines = ($fileStats | Measure-Object -Property Lines -Sum).Sum

Write-Host ""
Write-Host ("Total: {0:N0} lines across {1:N0} files" -f $totalLines, $totalFiles) -ForegroundColor Green
Write-Host ""

# Summary by top-level directory.
Write-Host "By directory:" -ForegroundColor Cyan
$byDirectory = $fileStats | Group-Object {
    $top = ($_.Directory -split '[\\/]')[0]
    if ($top) { $top } else { '(root)' }
} | ForEach-Object {
    [PSCustomObject]@{
        Directory = $_.Name
        Files     = $_.Count
        Lines     = ($_.Group | Measure-Object -Property Lines -Sum).Sum
    }
} | Sort-Object Lines -Descending

$byDirectory | Format-Table -AutoSize

# Summary by file extension.
Write-Host "By extension:" -ForegroundColor Cyan
$byExtension = $fileStats | Group-Object Extension | ForEach-Object {
    [PSCustomObject]@{
        Extension = $_.Name
        Files     = $_.Count
        Lines     = ($_.Group | Measure-Object -Property Lines -Sum).Sum
    }
} | Sort-Object Lines -Descending

$byExtension | Format-Table -AutoSize

# Largest files.
Write-Host "Top 10 files by line count:" -ForegroundColor Cyan
$fileStats | Sort-Object Lines -Descending | Select-Object -First 10 Lines, Relative | Format-Table -AutoSize
Read-Host "�� Enter ���˳�..."
