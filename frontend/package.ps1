param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtBin,
    [Parameter(Mandatory)][string]$OutputDirectory
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

$out = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $frontend $file) -Destination $out
}
& $deploy --release --no-translations --dir $out (Join-Path $out 'media_library_qt.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$help = @'
多媒体文件管理及检索系统 · Windows 演示

1. 把整个压缩包解压到可写目录，双击 media_library_qt.exe。
2. 在设置窗口输入本次演示口令，点击「继续」，再在登录窗口输入同一个口令。
3. 媒体库默认打开 30 条合成样例。增删会保存到同目录的 media_records.csv。
4. 原始样例的检索用例：导演 林川、演员 陈明、文件类型 MP4，预期命中两条。
5. 推荐页可选类别与类型。检索页的「性能对照」展示 10 万条合成数据的已记录实验。

口令用于课程演示。开始新一轮演示前如需恢复样例，请在关闭程序后备份或移走 media_records.csv。
'@
Set-Content -LiteralPath (Join-Path $out '使用说明.txt') -Value $help -Encoding UTF8
