param(
    [Parameter(Mandatory=$true)][string]$PythonDeps,
    [string]$OutputDirectory = '',
    [string]$CompilerDirectory = 'D:\32\ARM\ARMCC\Bin'
)
$ErrorActionPreference = 'Stop'
$project = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $env:TEMP ('h7_result_test_' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
foreach ($core in @('CM7','CM4')) {
    $object = Join-Path $OutputDirectory ($core + '_result_test.o')
    $image = Join-Path $OutputDirectory ($core + '_result_test.axf')
    $cpu = if ($core -eq 'CM7') { 'Cortex-M7' } else { 'Cortex-M4' }
    $compileArgs = @('--cpu',$cpu,'--c99','-O0','-I',(Join-Path $project 'Common\Inc'),
        '-I',(Join-Path $project "$core\Framework"),'-I',(Join-Path $project "$core\Platform"),
        '-I',(Join-Path $project "$core\Service"),'-I',(Join-Path $project "$core\Device\Inc"),
        '-c',(Join-Path $PSScriptRoot 'result_test.c'),'-o',$object)
    if ($core -eq 'CM4') { $compileArgs += '-DTEST_CM4' }
    & (Join-Path $CompilerDirectory 'armcc.exe') @compileArgs
    if ($LASTEXITCODE -ne 0) { throw "$core test compilation failed" }
    & (Join-Path $CompilerDirectory 'armlink.exe') $object --entry Result_TestRun `
        --scatter (Join-Path $PSScriptRoot 'result_test.sct') '--keep=*(.framework_init)' --no_scanlib -o $image
    if ($LASTEXITCODE -ne 0) { throw "$core test linking failed" }
    python (Join-Path $PSScriptRoot 'run_result_test.py') $image --core $core --deps $PythonDeps |
        Tee-Object -FilePath (Join-Path $PSScriptRoot ($core + '_result_test.log'))
    if ($LASTEXITCODE -ne 0) { throw "$core result migration tests failed" }
}
