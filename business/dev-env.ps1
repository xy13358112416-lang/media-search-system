param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$BuildTemp = "$env:SystemDrive\tmp"
)

$toolBin = Join-Path $MsysRoot 'ucrt64\bin'
if (-not (Test-Path -LiteralPath (Join-Path $toolBin 'g++.exe')) -or
    -not (Test-Path -LiteralPath (Join-Path $toolBin 'cmake.exe')) -or
    -not (Test-Path -LiteralPath (Join-Path $toolBin 'ninja.exe'))) {
    throw "未找到 UCRT64 工具链：$toolBin"
}
if (-not (Test-Path -LiteralPath $BuildTemp)) {
    New-Item -ItemType Directory -Path $BuildTemp -ErrorAction Stop | Out-Null
}
$env:PATH = $toolBin + ';' + $env:PATH
$env:TEMP = $BuildTemp
$env:TMP = $BuildTemp
Write-Host "已为当前终端设置 UCRT64 工具链和临时目录：$BuildTemp"
