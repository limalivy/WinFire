# ============================================================================
# 清除 PendingFileRenameOperations 中 WinFire 的残留条目
# 必须以管理员身份运行
# ============================================================================
#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager"
$valueName = "PendingFileRenameOperations"

$current = (Get-ItemProperty -Path $regPath -Name $valueName).$valueName

Write-Host "Current PFR entries: $($current.Count)"

# 找出并移除 WinFire 相关的 source+dest 配对项
$newValue = @()
$skipNext = $false
foreach ($entry in $current) {
    if ($entry -match "WinFire") {
        Write-Host "  Removing: $entry"
        $skipNext = $true
        continue
    }
    if ($skipNext) {
        Write-Host "  Removing pair: [$entry]"
        $skipNext = $false
        continue
    }
    $newValue += $entry
}

Write-Host "New PFR entries: $($newValue.Count)"
Set-ItemProperty -Path $regPath -Name $valueName -Value $newValue
Write-Host "[OK] Cleaned" -ForegroundColor Green
