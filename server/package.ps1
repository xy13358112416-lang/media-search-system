param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtBin,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SodiumDll
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$serverBuild = Join-Path $build 'server'
$qt = (Resolve-Path -LiteralPath $QtBin).Path
$deploy = Join-Path $qt 'windeployqt.exe'
$source = $PSScriptRoot
$exeNames = @(
    'media_server.exe',
    'media_admin_setup.exe',
    'media_person_import.exe',
    'media_person_source_config.exe',
    'media_demo_import.exe'
)

foreach ($exe in $exeNames) {
    if (!(Test-Path -LiteralPath (Join-Path $serverBuild $exe) -PathType Leaf)) {
        throw "Missing server build output: $exe"
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
$cache = Join-Path $build 'CMakeCache.txt'
$compilerLine = if (Test-Path -LiteralPath $cache) {
    (Select-String -LiteralPath $cache -Pattern '^CMAKE_CXX_COMPILER:(FILEPATH|STRING|UNINITIALIZED)=' |
        Select-Object -First 1).Line
} else { $null }
$compilerPath = if ($compilerLine) { $compilerLine.Substring($compilerLine.IndexOf('=') + 1) } else { $null }
$runtimeBin = if ($compilerPath -and (Test-Path -LiteralPath $compilerPath)) {
    Split-Path -Parent $compilerPath
} else {
    $compiler = Get-Command g++.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$compiler) { throw "Cannot locate the C++ runtime directory." }
    Split-Path -Parent $compiler.Source
}

$out = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
foreach ($exe in $exeNames) {
    Copy-Item -LiteralPath (Join-Path $serverBuild $exe) -Destination $out
}
foreach ($file in @('media-server.ini', 'start-server.cmd', '01-Setup-Admin.cmd', '01-Setup-Admin-Terminal.cmd')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $out
}
$sodiumName = Split-Path -Leaf $SodiumDll
Copy-Item -LiteralPath $SodiumDll -Destination (Join-Path $out $sodiumName)
if ($sodiumName -ne 'libsodium.dll') {
    Copy-Item -LiteralPath $SodiumDll -Destination (Join-Path $out 'libsodium.dll')
}
foreach ($runtimeDll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    $runtimePath = Join-Path $runtimeBin $runtimeDll
    if (!(Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
        throw "Missing C++ runtime DLL: $runtimePath"
    }
    Copy-Item -LiteralPath $runtimePath -Destination $out
}

& $deploy --qtpaths (Join-Path $qt 'qtpaths.exe') --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --compiler-runtime --dir $out (Join-Path $out 'media_admin_setup.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}
if (!(Test-Path -LiteralPath (Join-Path $out 'platforms/qwindows.dll') -PathType Leaf)) {
    throw "Missing Windows platform plugin for administrator setup."
}
if (!(Test-Path -LiteralPath (Join-Path $out 'sqldrivers/qsqlite.dll') -PathType Leaf)) {
    throw "Missing Qt SQLite driver in the server package."
}
Get-ChildItem -LiteralPath (Join-Path $out 'sqldrivers') -File |
    Where-Object Name -ne 'qsqlite.dll' |
    Remove-Item -Force
$networkInformation = Join-Path $out 'networkinformation'
if (Test-Path -LiteralPath $networkInformation) {
    Remove-Item -LiteralPath $networkInformation -Recurse -Force
}
$tls = Join-Path $out 'tls'
if (!(Test-Path -LiteralPath (Join-Path $tls 'qschannelbackend.dll') -PathType Leaf)) {
    throw "Missing Qt Schannel TLS backend in the server package."
}
Get-ChildItem -LiteralPath $tls -File |
    Where-Object Name -ne 'qschannelbackend.dll' |
    Remove-Item -Force
if (!(Test-Path -LiteralPath (Join-Path $tls 'qschannelbackend.dll') -PathType Leaf)) {
    throw "Windows HTTPS backend validation failed."
}

# windeployqt does not resolve all MSYS2 transitive DLLs. Walk PE imports and
# copy missing dependencies from the compiler runtime directory.
$objdump = Join-Path $runtimeBin 'objdump.exe'
if (!(Test-Path -LiteralPath $objdump -PathType Leaf)) {
    throw "Missing PE dependency tool: $objdump"
}
$pending = [System.Collections.Generic.Queue[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($file in Get-ChildItem -LiteralPath $out -Recurse -File |
         Where-Object Extension -in @('.exe', '.dll')) {
    $pending.Enqueue($file.FullName)
}
while ($pending.Count -gt 0) {
    $file = $pending.Dequeue()
    if (!$seen.Add($file)) { continue }
    $dependencies = & $objdump -p $file |
        Select-String -Pattern 'DLL Name:' |
        ForEach-Object { ($_.Line -split ':', 2)[1].Trim() }
    foreach ($dependency in $dependencies) {
        $packaged = Join-Path $out $dependency
        if (Test-Path -LiteralPath $packaged -PathType Leaf) {
            $pending.Enqueue($packaged)
            continue
        }
        $candidate = Join-Path $runtimeBin $dependency
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Copy-Item -LiteralPath $candidate -Destination $packaged
            $pending.Enqueue($packaged)
        }
    }
}

$docs = New-Item -ItemType Directory -Path (Join-Path $out 'docs')
foreach ($file in @('双端测试操作手册.md', '管理员模块开发.md', '管理员模块验收.md', '管理员初始化与升级测试.md', '在线媒体分类与日期排序.md')) {
    Copy-Item -LiteralPath (Join-Path $source "../docs/$file") -Destination $docs
}
foreach ($file in @('README.md', 'PERSON_SCHEMA.md', 'PERSON_IMPORT.md',
                    'PERSON_SOURCE_CONFIG.md', 'DEPLOYMENT.md')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $docs
}

$examples = New-Item -ItemType Directory -Path (Join-Path $out 'examples')
Copy-Item -LiteralPath (Join-Path $source '../sample-data/person/person-sources.example.json') `
    -Destination $examples
$demoExamples = New-Item -ItemType Directory -Path (Join-Path $examples 'demo')
Copy-Item -LiteralPath (Join-Path $source '../sample-data/person/import-demo.synthetic.json') `
    -Destination $demoExamples

$formal = New-Item -ItemType Directory -Path (Join-Path $out 'data/formal')
$demo = New-Item -ItemType Directory -Path (Join-Path $out 'data/demo')
Set-Content -LiteralPath (Join-Path $formal 'README.txt') -Encoding UTF8 -Value @'
Formal server database directory.
Never place synthetic demo data or demo accounts in this database.
'@
Set-Content -LiteralPath (Join-Path $demo 'README.txt') -Encoding UTF8 -Value @'
Demo-only database directory.
Create this database only with media_demo_import and an approved synthetic fixture.
'@

$readme = @'
多媒体检索系统 · Windows 服务端演示包

1. 阅读 docs/DEPLOYMENT.md，正式库和演示库必须放在不同目录。
   首次创建管理员双击 01-Setup-Admin.cmd，在本机 Qt 窗口填写账号和两次密码。
   已有管理员时初始化入口关闭，直接使用已有账号登录。
   终端交互入口为 01-Setup-Admin-Terminal.cmd，逐项输入账号及两次隐藏密码。
2. 服务端运行前将 MEDIA_SERVER_DEEPSEEK_API_KEY 设置在服务器进程环境中。
3. 发布包不包含数据库、真实人物映射、真实资讯 feed 清单、API Key 或客户端程序。
4. 首次初始化、账号、人物导入、feed 配置、升级、备份和重启步骤见 docs/DEPLOYMENT.md。
'@
Set-Content -LiteralPath (Join-Path $out 'SERVER_README.txt') -Value $readme -Encoding UTF8

# Smoke-test runtime discovery without falling back to the developer's PATH.
$oldPath = $env:PATH
$env:PATH = $out + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
try {
    foreach ($exe in $exeNames | Where-Object { $_ -ne 'media_admin_setup.exe' }) {
        & (Join-Path $out $exe) --help | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "$exe failed to start from the package."
        }
    }
} finally {
    $env:PATH = $oldPath
}

Write-Host "Server package created: $out"
