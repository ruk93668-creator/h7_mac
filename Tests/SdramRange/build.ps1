# 两种CM7冷启动配置均完整重编，最后保留默认TEST产物
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$project = Join-Path $projectRoot 'MDK-ARM/h7_mac.uvprojx'
$temporaryProject = Join-Path $projectRoot 'MDK-ARM/sdram_range_check.uvprojx'
$keil = 'D:/32/UV4/UV4.exe'

function Build-Target([string] $file, [string] $target, [string] $logName) {
    $log = Join-Path $PSScriptRoot $logName
    $arguments = @('-r', ('"' + $file + '"'), '-t', $target, '-o', ('"' + $log + '"'))
    $process = Start-Process -FilePath $keil -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw "$target full rebuild failed; see $log"
    }
    Write-Output "$target $logName : 0 Error, 0 Warning"
}

if (Test-Path -LiteralPath $temporaryProject) { throw 'Temporary project already exists; preserve it' }
$before = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$xmlText = [System.IO.File]::ReadAllText($project)
$start = $xmlText.IndexOf('<TargetName>h7_mac_CM7</TargetName>')
$defineStart = $xmlText.IndexOf('<Define>', $start) + '<Define>'.Length
$defineEnd = $xmlText.IndexOf('</Define>', $defineStart)
if ($start -lt 0 -or $defineEnd -lt $defineStart) { throw 'CM7 define not found' }
$xmlText = $xmlText.Insert($defineEnd, ',USB_MODE_BOOT_UVC=1')
[System.IO.File]::WriteAllText($temporaryProject, $xmlText, (New-Object System.Text.UTF8Encoding($false)))
try {
    Build-Target $temporaryProject 'h7_mac_CM7' 'cm7_uvc_keil.log'
    Build-Target $project 'h7_mac_CM4' 'cm4_keil.log'
    Build-Target $project 'h7_mac_CM7' 'cm7_keil.log'
} finally {
    # 仅删除本脚本创建的独立临时工程，不接触正式工程或用户目录
    Remove-Item -LiteralPath $temporaryProject
    if ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $before) {
        throw 'Formal project changed during build'
    }
}
