param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtBin,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SodiumDll
)

$ErrorActionPreference = 'Stop'
$frontend = Join-Path (Resolve-Path -LiteralPath $BuildDirectory).Path 'frontend'
$qt = (Resolve-Path -LiteralPath $QtBin).Path
$deploy = Join-Path $qt 'windeployqt.exe'
$files = @('media_library_qt.exe', 'media_records_sample.csv')

foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $frontend $file) -PathType Leaf)) {
        throw "Missing build output: $file"
    }
}
if (!(Test-Path -LiteralPath $deploy -PathType Leaf)) {
    throw "Missing Qt deployment tool: $deploy"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Choose a new output directory: $OutputDirectory"
}

if (!$SodiumDll) {
    $runtime = Get-Command libsodium.dll -CommandType Application -ErrorAction SilentlyContinue
    if (!$runtime) {
        throw "Set -SodiumDll to the libsodium.dll used by the build."
    }
    $SodiumDll = $runtime.Source
}
$SodiumDll = (Resolve-Path -LiteralPath $SodiumDll).Path

$out = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $frontend $file) -Destination $out
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'media-client.ini') -Destination $out
& $deploy --qtpaths (Join-Path $qt 'qtpaths.exe') --release --no-translations --dir $out (Join-Path $out 'media_library_qt.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# Preserve the imported DLL name (for example libsodium-26.dll), as well as
# the unversioned name used by some supported toolchains.
Copy-Item -LiteralPath $SodiumDll -Destination (Join-Path $out (Split-Path -Leaf $SodiumDll))
if ((Split-Path -Leaf $SodiumDll) -ne 'libsodium.dll') {
    Copy-Item -LiteralPath $SodiumDll -Destination (Join-Path $out 'libsodium.dll')
}
if (!(Test-Path -LiteralPath (Join-Path $out 'sqldrivers/qsqlite.dll') -PathType Leaf)) {
    throw "Missing Qt SQLite driver in the package."
}

$help = @'
多媒体文件管理及检索系统 · Windows 演示

1. 把整个压缩包解压到可写目录，双击 media_library_qt.exe。
2. 先选择用户登录或管理员登录，再连接 media-client.ini 配置的服务器，输入服务器用户名和密码；所有页面共用本次登录。
3. 在线媒体库读取本人和公开仓库，浏览和检索记录保存在服务器。无账号请联系服务器管理员。
4. 检索可选择全部文件类型、未记录文件类型或已知格式，发行时间支持年份、年月与完整日期。
5. 动态推荐页可选类别与类型。单机模式的检索页「性能对照」展示 10 万条合成数据的已记录实验。
6. 服务器换地址时，在登录页点击「更改服务器」；成功登录后记住地址和用户名，不记住密码。
7. 需要原来的单机功能时，用 media_library_qt.exe --local 启动，单机账户、CSV 和历史与服务器分开。

默认服务器地址 192.168.43.8:8765。服务器必须先启动；客户端启动不负责启动远端电脑的服务器。
'@
Set-Content -LiteralPath (Join-Path $out '使用说明.txt') -Value $help -Encoding UTF8

$docs = New-Item -ItemType Directory -Path (Join-Path $out 'docs')
foreach ($file in @('双端测试操作手册.md', '管理员模块开发.md', '管理员模块验收.md', '管理员初始化与升级测试.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../docs/$file") -Destination $docs
}
Set-Content -LiteralPath (Join-Path $out 'Start-Client.cmd') -Encoding Ascii -Value '@echo off', 'start "" "%~dp0media_library_qt.exe"'
