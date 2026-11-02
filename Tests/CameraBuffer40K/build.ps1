$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$project = Join-Path $root 'MDK-ARM/h7_mac.uvprojx'
$tempProject = Join-Path $root 'MDK-ARM/camera_buffer_40k_check.uvprojx'
$before = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$xmlText = [System.IO.File]::ReadAllText($project)
$start = $xmlText.IndexOf('<TargetName>h7_mac_CM7</TargetName>')
$defineStart = $xmlText.IndexOf('<Define>', $start) + '<Define>'.Length
$defineEnd = $xmlText.IndexOf('</Define>', $defineStart)
if ($start -lt 0 -or $defineEnd -lt $defineStart) { throw 'CM7 defines missing' }
$defines = $xmlText.Substring($defineStart, $defineEnd - $defineStart)
$clean = (($defines.Split(',') | Where-Object { $_ -notmatch '^USB_MODE_BOOT_UVC=' }) -join ',')
function Build([string] $file, [string] $target, [string] $logName, [string] $action) {
    $log = Join-Path $PSScriptRoot $logName
    $process = Start-Process -FilePath 'D:/32/UV4/UV4.exe' -ArgumentList @($action, ('"'+$file+'"'), '-t', $target, '-o', ('"'+$log+'"')) -WindowStyle Hidden -PassThru -Wait
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') { throw "Build failed: $log" }
    Write-Output "$target $logName : 0 Error, 0 Warning"
}
if (Test-Path -LiteralPath $tempProject) { throw 'Preserve existing temporary project' }
try {
    foreach ($mode in @(0, 1)) {
        $variant = $xmlText.Substring(0,$defineStart) + $clean + ",USB_MODE_BOOT_UVC=$mode" + $xmlText.Substring($defineEnd)
        [System.IO.File]::WriteAllText($tempProject, $variant, (New-Object System.Text.UTF8Encoding($false)))
        $name = if ($mode -eq 0) {'cm7_test_keil.log'} else {'cm7_uvc_keil.log'}
        Build $tempProject 'h7_mac_CM7' $name '-r'
    }
    Build $project 'h7_mac_CM4' 'cm4_keil.log' '-r'
} finally {
    if (Test-Path -LiteralPath $tempProject) { Remove-Item -LiteralPath $tempProject }
    if ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $before) { throw 'Formal project changed' }
}
# Restore generated RTE naming and the user's selected CM7 build configuration.
Build $project 'h7_mac_CM7' 'cm7_current_keil.log' '-b'
