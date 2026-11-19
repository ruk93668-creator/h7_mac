# Read-only identity discovery. No drive letters or disk numbers are assumed.
$ErrorActionPreference = 'Stop'
$found = @()
foreach ($disk in Get-CimInstance Win32_DiskDrive) {
    if ($disk.PNPDeviceID -notlike 'USBSTOR*') { continue }
    $chain = @(); $current = $disk.PNPDeviceID; $matched = $false
    for ($depth = 0; $depth -lt 12 -and $current; $depth++) {
        $chain += $current
        if ($current -match 'VID_1209&PID_0001') { $matched = $true; break }
        try { $current = (Get-PnpDeviceProperty -InstanceId $current -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data } catch { break }
    }
    if (-not $matched) { continue }
    $volumes = @()
    foreach ($partition in Get-CimAssociatedInstance -InputObject $disk -Association Win32_DiskDriveToDiskPartition) {
        foreach ($volume in Get-CimAssociatedInstance -InputObject $partition -Association Win32_LogicalDiskToPartition) {
            $volumes += [ordered]@{ drive = $volume.DeviceID; filesystem = $volume.FileSystem; size = [uint64]$volume.Size; volume_name = $volume.VolumeName }
        }
    }
    $found += [ordered]@{ number = [int]$disk.Index; path = $disk.DeviceID; model = $disk.Model; size = [uint64]$disk.Size; pnp_id = $disk.PNPDeviceID; parent_chain = $chain; volumes = $volumes }
}
$result = [ordered]@{ observed_at = (Get-Date).ToString('o'); targets = @($found) }
$destination = Join-Path $PSScriptRoot '../../output/MSC4-readonly/windows_identity.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
$result | ConvertTo-Json -Depth 8
