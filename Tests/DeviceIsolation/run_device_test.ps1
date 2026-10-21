param(
    [Parameter(Mandatory=$true)][string]$PythonDeps,
    [string]$OutputDirectory = '',
    [string]$CompilerDirectory = 'D:\32\ARM\ARMCC\Bin',
    [ValidateSet('CM7','CM4')][string]$Core = 'CM7'
)
$ErrorActionPreference = 'Stop'
$project = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $env:TEMP ('h7_device_test_' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$object = Join-Path $OutputDirectory 'device_test.o'
$image = Join-Path $OutputDirectory 'device_test.axf'
$source = Join-Path $PSScriptRoot 'device_test.c'
$common = Join-Path $project 'Common\Inc'
$deviceInclude = Join-Path $project "$Core\Device\Inc"
$cpu = if ($Core -eq 'CM4') { 'Cortex-M4' } else { 'Cortex-M7' }
$reportDirectory = if ($Core -eq 'CM4') { Join-Path $PSScriptRoot 'CM4' } else { $PSScriptRoot }
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$compiler = Join-Path $CompilerDirectory 'armcc.exe'
$linker = Join-Path $CompilerDirectory 'armlink.exe'
@(
    "& '$compiler' --cpu $cpu --c99 -O0 $(if ($Core -eq 'CM4') { '-DTEST_CM4 ' })-I '$common' -I '$deviceInclude' -c '$source' -o '$object'",
    "& '$linker' '$object' --entry Device_TestRun --ro-base 0x08000000 --rw-base 0x20000000 --no_scanlib -o '$image'"
) | Set-Content (Join-Path $reportDirectory 'compile_commands.txt') -Encoding UTF8
$compileArgs = @('--cpu',$cpu,'--c99','-O0','-I',$common,'-I',$deviceInclude,'-c',$source,'-o',$object)
if ($Core -eq 'CM4') { $compileArgs += '-DTEST_CM4' }
$compileOutput = & $compiler @compileArgs 2>&1
$compileCode = $LASTEXITCODE
'Current isolated test build:' | Set-Content (Join-Path $reportDirectory 'build_output.txt') -Encoding UTF8
$compileOutput | Add-Content (Join-Path $reportDirectory 'build_output.txt')
"armcc exit code: $compileCode" | Add-Content (Join-Path $reportDirectory 'build_output.txt')
if ($compileCode -ne 0) { $compileOutput | Write-Output; throw 'Device test compilation failed' }
$linkOutput = & $linker $object --entry Device_TestRun --ro-base 0x08000000 --rw-base 0x20000000 --no_scanlib -o $image 2>&1
$linkCode = $LASTEXITCODE
$linkOutput | Add-Content (Join-Path $reportDirectory 'build_output.txt')
"armlink exit code: $linkCode" | Add-Content (Join-Path $reportDirectory 'build_output.txt')
if ($linkCode -ne 0) { $linkOutput | Write-Output; throw 'Device test linking failed' }
$compileOutput | Write-Output
$linkOutput | Write-Output
python (Join-Path $PSScriptRoot 'run_device_test.py') $image --deps $PythonDeps --report $reportDirectory --core $Core |
    Tee-Object -FilePath (Join-Path $reportDirectory 'execution_output.txt')
if ($LASTEXITCODE -ne 0) { throw 'Device isolation acceptance failed; see report' }
