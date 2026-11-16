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
& $deploy --release --no-translations --dir $out (Join-Path $out 'media_library_qt.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $SodiumDll -Destination (Join-Path $out 'libsodium.dll')
if (!(Test-Path -LiteralPath (Join-Path $out 'sqldrivers/qsqlite.dll') -PathType Leaf)) {
    throw "Missing Qt SQLite driver in the package."
}

$help = @'
多媒体文件管理及检索系统 · Windows 演示

1. 把整个压缩包解压到可写目录，双击 media_library_qt.exe。
2. 启动后先选用户登录或管理员登录。首次使用在管理员入口创建管理员；初始化后可在用户登录页注册。
3. 媒体库默认打开 500 条作品元数据，不包含音视频文件。增删会保存到同目录的 media_records.csv。
4. 样例作品按音乐、电影、电视剧分类。样例的实际文件格式未收录，可用「格式未知」筛选。
5. 推荐页可选类别与类型。检索页的「性能对照」展示 10 万条合成数据的已记录实验。
6. 登录后点击右上角「媒体助手」，按片名或想看的氛围检索本地内容。可切换放松、专注、探索，也可恢复默认或关闭动效。氛围标签目前覆盖部分样例。
7. 资源仓库页使用服务器地址、端口和独立的网络账号。请由服务器管理员创建演示账号。智能推荐由仓库服务器处理。
8. 点击「关怀演示」可试用关注中心、全站热榜、个人足迹和动态推荐。该窗口使用模拟数据，不写服务器。

账户和浏览历史存放在应用本地数据目录的 accounts.sqlite。开始新一轮演示前如需恢复样例，请在关闭程序后备份或移走 media_records.csv。
'@
Set-Content -LiteralPath (Join-Path $out '使用说明.txt') -Value $help -Encoding UTF8
