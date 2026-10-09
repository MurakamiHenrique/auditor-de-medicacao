param(
    [ValidateSet('build','upload','clean')][string]$Action = 'build',
    [string]$Port = ''
)
# Windows shortcut for tools/build.py, which also runs on macOS and Linux.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = Join-Path $projectRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) { throw 'Crie .venv e instale platformio primeiro; veja README.md.' }
$buildArgs = @((Join-Path $PSScriptRoot 'build.py'), $Action)
if ($Port) { $buildArgs += @('--port', $Port) }
& $pythonPath @buildArgs
exit $LASTEXITCODE
