param([Parameter(Mandatory=$true)][string]$PythonDeps,[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$root=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $OutputDirectory){$OutputDirectory=Join-Path $env:TEMP ('h7_bsp_test_'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$object=Join-Path $OutputDirectory 'bsp_test.o';$image=Join-Path $OutputDirectory 'bsp_test.axf'
$compileArgs=@('--cpu','Cortex-M4','--c99','-O0','-I',(Join-Path $PSScriptRoot 'stubs'),'-I',(Join-Path $root 'CM4\BSP'),'-I',(Join-Path $root 'CM4\Framework'),'-I',(Join-Path $root 'Common\Inc'),'-c',(Join-Path $PSScriptRoot 'bsp_test.c'),'-o',$object)
& D:\32\ARM\ARMCC\Bin\armcc.exe @compileArgs
if($LASTEXITCODE -ne 0){throw 'BSP test compilation failed'}
& D:\32\ARM\ARMCC\Bin\armlink.exe $object --entry Bsp_TestRun --scatter (Join-Path $root 'Tests\ResultMigration\result_test.sct') '--keep=*(.framework_init)' --no_scanlib -o $image
if($LASTEXITCODE -ne 0){throw 'BSP test linking failed'}
python (Join-Path $PSScriptRoot 'run_test.py') $image --deps $PythonDeps --report $PSScriptRoot | Tee-Object -FilePath (Join-Path $PSScriptRoot 'execution_output.txt')
if($LASTEXITCODE -ne 0){throw 'BSP isolation test failed'}
