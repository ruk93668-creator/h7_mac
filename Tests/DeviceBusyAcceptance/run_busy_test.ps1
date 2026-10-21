param([Parameter(Mandatory=$true)][string]$PythonDeps,[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) {$OutputDirectory=Join-Path $env:TEMP ('h7_device_busy_'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$object=Join-Path $OutputDirectory 'busy_test.o';$image=Join-Path $OutputDirectory 'busy_test.axf'
$source=Join-Path $PSScriptRoot 'busy_test.c'
$scatter=Join-Path $root 'Tests\DeviceAutoIsolation\auto_test.sct'
$compileArgs=@('--cpu','Cortex-M4','--c99','-O0','-I',(Join-Path $root 'Common\Inc'),'-I',(Join-Path $root 'Board\Inc'),'-I',(Join-Path $root 'CM4\Device\Inc'),'-I',(Join-Path $root 'CM4\Framework'),'-c',$source,'-o',$object)
'Isolated ARMCC5 build:' | Set-Content (Join-Path $PSScriptRoot 'build_output.txt') -Encoding UTF8
$messages=& D:\32\ARM\ARMCC\Bin\armcc.exe @compileArgs 2>&1
$code=$LASTEXITCODE;$messages | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
"armcc exit=$code" | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
if ($code -ne 0) {$messages | Write-Output;throw 'Compile failed'}
$messages=& D:\32\ARM\ARMCC\Bin\armlink.exe $object --entry Busy_TestRun --scatter $scatter '--keep=*(.device_export)' '--keep=*(.framework_init)' --no_scanlib -o $image 2>&1
$code=$LASTEXITCODE;$messages | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
"armlink exit=$code" | Add-Content (Join-Path $PSScriptRoot 'build_output.txt')
if ($code -ne 0) {$messages | Write-Output;throw 'Link failed'}
python (Join-Path $PSScriptRoot 'run_busy_test.py') $image --deps $PythonDeps --report $PSScriptRoot | Tee-Object -FilePath (Join-Path $PSScriptRoot 'execution_output.txt')
if ($LASTEXITCODE -ne 0) {throw 'Device BUSY acceptance failed'}
