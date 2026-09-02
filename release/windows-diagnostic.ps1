[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$lines = [System.Collections.Generic.List[string]]::new()

function Add-ReportLine {
    param([string]$Text = '')
    $lines.Add($Text)
}

Add-ReportLine 'Pico W Bluetooth keyboard and mouse Windows diagnostic'
Add-ReportLine ('Captured: ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz'))
Add-ReportLine 'Expected runtime USB ID: VID_CAFE&PID_4008'
Add-ReportLine ''

try {
    $os = Get-CimInstance Win32_OperatingSystem
    Add-ReportLine ('Windows: {0} / version {1} / build {2}' -f
        $os.Caption, $os.Version, $os.BuildNumber)
} catch {
    Add-ReportLine ('Windows version query failed: ' + $_.Exception.Message)
}

Add-ReportLine ''
Add-ReportLine 'BOOTSEL volume:'
try {
    $bootVolumes = @(Get-Volume -ErrorAction Stop | Where-Object {
        $_.FileSystemLabel -eq 'RPI-RP2'
    })
    if ($bootVolumes.Count -eq 0) {
        Add-ReportLine '  RPI-RP2 not present (normal while bridge firmware runs).'
    } else {
        foreach ($volume in $bootVolumes) {
            Add-ReportLine ('  PRESENT: drive {0}:, health {1}, filesystem {2}' -f
                $volume.DriveLetter, $volume.HealthStatus, $volume.FileSystem)
        }
    }
} catch {
    Add-ReportLine ('  Volume query failed: ' + $_.Exception.Message)
}

Add-ReportLine ''
Add-ReportLine 'Pico W Bluetooth keyboard and mouse runtime devices:'
try {
    $matches = @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
        $_.InstanceId -match 'VID_CAFE&PID_4008'
    } | Sort-Object Class, FriendlyName, InstanceId)
    if ($matches.Count -eq 0) {
        Add-ReportLine '  NOT FOUND.'
    } else {
        foreach ($device in $matches) {
            Add-ReportLine ('  Status={0}; Class={1}; Name={2}' -f
                $device.Status, $device.Class, $device.FriendlyName)
            Add-ReportLine ('    ID=' + $device.InstanceId)
        }
    }

    $keyboardSeen = @($matches | Where-Object {
        $_.InstanceId -match 'MI_00' -or $_.FriendlyName -match 'keyboard'
    }).Count -gt 0
    $mouseSeen = @($matches | Where-Object {
        $_.InstanceId -match 'MI_01' -or $_.FriendlyName -match 'mouse'
    }).Count -gt 0

    Add-ReportLine ''
    if ($matches.Count -eq 0) {
        Add-ReportLine 'RESULT: Bridge runtime USB device was not detected.'
    } elseif ($keyboardSeen -and $mouseSeen) {
        Add-ReportLine 'RESULT: Bridge runtime plus keyboard and mouse interfaces were detected.'
    } else {
        Add-ReportLine ('RESULT: Bridge runtime detected; keyboard indicator={0}, mouse indicator={1}.' -f
            $keyboardSeen, $mouseSeen)
    }
} catch {
    Add-ReportLine ('  PnP query failed: ' + $_.Exception.Message)
    Add-ReportLine 'RESULT: Diagnostic could not read Windows PnP state.'
}

$desktop = [Environment]::GetFolderPath('Desktop')
if ([string]::IsNullOrWhiteSpace($desktop)) {
    $desktop = $PSScriptRoot
}
$reportPath = Join-Path $desktop ('pico-w-bluetooth-keyboard-mouse-diagnostic-{0}.txt' -f
    (Get-Date).ToString('yyyyMMdd-HHmmss'))
$lines | Set-Content -LiteralPath $reportPath -Encoding utf8

$lines | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host ('Saved report: ' + $reportPath)
