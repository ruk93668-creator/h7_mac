param(
    [string]$PythonDeps = '',
    [string]$OutputDirectory = '',
    [string]$CompilerDirectory = 'D:\32\ARM\ARMCC\Bin'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $env:TEMP ('h7_timer_phase2_build_' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$source = Join-Path $PSScriptRoot 'phase2_test.c'
$object = Join-Path $outputPath 'phase2_test.o'
$image = Join-Path $outputPath 'phase2_test.axf'
$armcc = Join-Path $CompilerDirectory 'armcc.exe'
$armlink = Join-Path $CompilerDirectory 'armlink.exe'
$common = Join-Path $projectRoot 'Common\Inc'
$platform = Join-Path $projectRoot 'CM7\Platform'
$commands = @(
    "& '$armcc' --cpu Cortex-M7 --c99 -O0 -I '$common' -I '$platform' -c '$source' -o '$object'",
    "& '$armlink' '$object' --entry Test_Run --ro-base 0x08000000 --rw-base 0x20000000 --no_scanlib -o '$image'"
)
$commands | Set-Content (Join-Path $PSScriptRoot 'compile_commands.txt') -Encoding UTF8
$commands | Write-Output
$buildLog = Join-Path $PSScriptRoot 'build_output.txt'
'Isolated ARMCC build (current run):' | Set-Content $buildLog -Encoding UTF8
& $armcc --cpu Cortex-M7 --c99 -O0 -I $common -I $platform -c $source -o $object 2>&1 | Tee-Object -FilePath $buildLog -Append
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
'armcc: exit code 0; no diagnostics.' | Add-Content $buildLog
& $armlink $object --entry Test_Run --ro-base 0x08000000 --rw-base 0x20000000 --no_scanlib -o $image 2>&1 | Tee-Object -FilePath $buildLog -Append
if ($LASTEXITCODE -ne 0) { throw 'Test linking failed' }
'armlink: exit code 0; no diagnostics.' | Add-Content $buildLog
$runnerArgs = @((Join-Path $PSScriptRoot 'run_phase2.py'), $image, '--report', $PSScriptRoot)
if ($PythonDeps) { $runnerArgs += @('--deps', $PythonDeps) }
python @runnerArgs | Tee-Object -FilePath (Join-Path $PSScriptRoot 'execution_output.txt')
if ($LASTEXITCODE -ne 0) { throw 'Timer phase 2 acceptance failed; see report' }
