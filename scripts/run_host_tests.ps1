#requires -Version 5.1
<#
.SYNOPSIS
  TianshangPulse host 单测驱动（不需硬件、不需 ESP-IDF 工具链）。

.DESCRIPTION
  自动探测 C 编译器（gcc → clang → python -m ziglang cc），编译并运行：
    - tests/host/test_offline_cache.c   环形缓冲语义（A5 精确 ACK-pop）
    - tests/host/test_protocol.c        协议契约向量（AGENTS §9.5 DoD #1）

  任一测试非零退出、或输出缺少 "ALL PASS"，整体失败（exit 1）。

.PARAMETER Cc
  显式指定编译器可执行文件（跳过自动探测）。例：-Cc clang

.EXAMPLE
  pwsh scripts/run_host_tests.ps1
  pwsh scripts/run_host_tests.ps1 -Cc gcc
#>
[CmdletBinding()]
param(
    [string]$Cc = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root 'build/host-tests'

function Test-Command([string]$name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Resolve-Compiler {
    param([string]$Explicit)
    if ($Explicit) {
        if (-not (Test-Command $Explicit)) { throw "指定的编译器不可用: $Explicit" }
        return ,@($Explicit)
    }
    if (Test-Command 'gcc')   { return ,@('gcc') }
    if (Test-Command 'clang') { return ,@('clang') }
    if ((Test-Command 'python') -and (& python -c "import ziglang" 2>$null; $LASTEXITCODE -eq 0)) {
        return ,@('python', '-m', 'ziglang', 'cc')
    }
    if ((Test-Command 'py') -and (& py -c "import ziglang" 2>$null; $LASTEXITCODE -eq 0)) {
        return ,@('py', '-m', 'ziglang', 'cc')
    }
    throw '未找到 C 编译器。请安装 gcc / clang，或 pip install ziglang（推荐）。'
}

$compiler = Resolve-Compiler -Explicit $Cc
$ccName = $compiler -join ' '
Write-Host "==> 编译器: $ccName" -ForegroundColor Cyan

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# 每个测试：测试源 + 被测源 + 包含路径
$tests = @(
    @{
        Name = 'test_offline_cache'
        Srcs = @('tests/host/test_offline_cache.c', 'firmware/main/ble/offline_cache.c')
    },
    @{
        Name = 'test_protocol'
        Srcs = @('tests/host/test_protocol.c', 'firmware/main/ble/protocol.c')
    }
)

$failed = @()
foreach ($t in $tests) {
    $exe = Join-Path $outDir ("{0}.exe" -f $t.Name)
    $args = @()
    $args += $t.Srcs
    $args += @('-I', 'tests/host/stubs', '-I', 'firmware/main', '-O2', '-Wall', '-Wextra',
               '-Wno-unused-parameter', '-o', $exe)

    Write-Host "==> 编译 $($t.Name)" -ForegroundColor Cyan
    Push-Location $root
    try {
        & $compiler[0] @($compiler[1..($compiler.Count - 1)]) @args
        $buildRc = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($buildRc -ne 0) {
        Write-Host "!! 编译失败: $($t.Name) (rc=$buildRc)" -ForegroundColor Red
        $failed += "$($t.Name): 编译失败"
        continue
    }

    Write-Host "==> 运行 $($t.Name)" -ForegroundColor Cyan
    $output = & $exe 2>&1
    $runRc = $LASTEXITCODE
    $output | ForEach-Object { Write-Host "    $_" }

    if ($runRc -ne 0) {
        $failed += "$($t.Name): 运行失败 (rc=$runRc)"
    } elseif (($output -join "`n") -notmatch 'ALL PASS') {
        $failed += "$($t.Name): 输出缺少 ALL PASS"
    }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host "host tests FAILED:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'host tests: ALL PASS' -ForegroundColor Green
exit 0
