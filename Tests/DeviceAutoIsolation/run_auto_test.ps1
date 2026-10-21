param(
    [Parameter(Mandatory=$true)][string]$PythonDeps,
    [string]$OutputDirectory = '',
    [string]$CompilerDirectory = 'D:\32\ARM\ARMCC\Bin'
)
$ErrorActionPreference='Stop'
$project=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $env:TEMP ('h7_device_auto_'+[guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$object=Join-Path $OutputDirectory 'auto_test.o'
$image=Join-Path $OutputDirectory 'auto_test.axf'
$source=Join-Path $PSScriptRoot 'auto_test.c'
$scatter=Join-Path $PSScriptRoot 'auto_test.sct'
$compiler=Join-Path $CompilerDirectory 'armcc.exe'
$linker=Join-Path $CompilerDirectory 'armlink.exe'
$compileArgs=@('--cpu','Cortex-M4','--c99','-O0','-I',(Join-Path $project 'Common\Inc'),
    '-I',(Join-Path $project 'Board\Inc'),'-I',(Join-Path $project 'CM4\Device\Inc'),
    '-I',(Join-Path $project 'CM4\Framework'),'-c',$source,'-o',$object)
@(
    "$compiler --cpu Cortex-M4 --c99 -O0 -I Common/Inc -I Board/Inc -I CM4/Device/Inc -I CM4/Framework -c $source -o $object",
    "$linker $object --entry Auto_TestRun --scatter $scatter --keep=*(.device_export) --keep=*(.framework_init) --no_scanlib -o $image"
) | Set-Content (Join-Path $PSScriptRoot 'compile_commands.txt') -Encoding UTF8
'Current isolated build:' | Set-Content (Join-Path $PSScriptRoot 'build_output.txt') -Encoding UTF8
$diagnostics=& $compiler @compileArgs 2>&1
$code=$LASTEXITCODE
$diagnostics | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
"armcc exit=$code" | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
if ($code -ne 0) { $diagnostics | Write-Output; throw 'Compile failed' }
$diagnostics=& $linker $object --entry Auto_TestRun --scatter $scatter '--keep=*(.device_export)' '--keep=*(.framework_init)' --no_scanlib -o $image 2>&1
$code=$LASTEXITCODE
$diagnostics | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
"armlink exit=$code" | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
if ($code -ne 0) { $diagnostics | Write-Output; throw 'Link failed' }
python (Join-Path $PSScriptRoot 'run_auto_test.py') $image --deps $PythonDeps --report $PSScriptRoot |
    Tee-Object -FilePath (Join-Path $PSScriptRoot 'execution_output.txt')
if ($LASTEXITCODE -ne 0) { throw 'Unexpected test failure; see report' }
